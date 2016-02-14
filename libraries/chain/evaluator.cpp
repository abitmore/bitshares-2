/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {
database& generic_evaluator::db()const { return trx_state->db(); }

   operation_result generic_evaluator::start_evaluate( transaction_evaluation_state& eval_state, const operation& op, bool apply )
   { try {
      trx_state   = &eval_state;
      //check_required_authorities(op);
      auto result = evaluate( op );

      if( apply ) result = this->apply( op );
      return result;
   } FC_CAPTURE_AND_RETHROW() }

   void generic_evaluator::prepare_fee(account_id_type account_id, asset fee)
   {
      const database& d = db();
      fee_from_account = fee;
      FC_ASSERT( fee.amount >= 0 );
      fee_paying_account = &account_id(d);
      fee_paying_account_statistics = &fee_paying_account->statistics(d);

      fee_asset = &fee.asset_id(d);
      fee_asset_dyn_data = &fee_asset->dynamic_asset_data_id(d);

      if( d.head_block_time() > HARDFORK_419_TIME )
      {
         FC_ASSERT( fee_paying_account->is_authorized_asset( *fee_asset, d ), "Account ${acct} '${name}' attempted to pay fee by using asset ${a} '${sym}', which is unauthorized due to whitelist / blacklist",
            ("acct", fee_paying_account->id)("name", fee_paying_account->name)("a", fee_asset->id)("sym", fee_asset->symbol) );
      }

      if( fee_from_account.asset_id == asset_id_type() )
         core_fee_paid = fee_from_account.amount;
      else
      {
         asset fee_from_pool = fee_from_account * fee_asset->options.core_exchange_rate;
         FC_ASSERT( fee_from_pool.asset_id == asset_id_type() );
         core_fee_paid = fee_from_pool.amount;
         FC_ASSERT( core_fee_paid <= fee_asset_dyn_data->fee_pool, "Fee pool balance of '${b}' is less than the ${r} required to convert ${c}",
                    ("r", db().to_pretty_string( fee_from_pool))("b",db().to_pretty_string(fee_asset_dyn_data->fee_pool))("c",db().to_pretty_string(fee)) );
      }
   }

   void generic_evaluator::prepare_fee_from_coin_seconds(const operation& o)
   {
      const auto& fee_options = db().get_global_properties().parameters.get_coin_seconds_as_fees_options();
      const auto& max_op_fee = fee_options.max_fee_from_coin_seconds_by_operation;
      dlog( "max_op_fee.size()=${s}, o.which=${w}", ("s",max_op_fee.size())("w",o.which()) );
      if( max_op_fee.size() > o.which() ) dlog( "max_op_fee[o.which()]=${f}", ("f",max_op_fee[o.which()]) );
      if( max_op_fee.size() > o.which() && max_op_fee[o.which()] > 0 ) // if fee can be paid with coin seconds
      {
         const asset& core_balance = db().get_balance( fee_paying_account->get_id(), asset_id_type() );
         const auto payer_membership = fee_paying_account->get_membership( db().head_block_time() );
         coin_seconds_earned = fee_paying_account_statistics->compute_coin_seconds_earned(
                                       core_balance, db().head_block_time() );
         dlog( "coin_seconds_earned=${c}", ("c",coin_seconds_earned) );
         if( coin_seconds_earned > 0 ) // if payer have some coin seconds to pay
         {
            coin_seconds_as_fees_rate = fee_options.coin_seconds_as_fees_rate[payer_membership];
            fc::uint128_t coin_seconds_to_fees = coin_seconds_earned;
            coin_seconds_to_fees /= coin_seconds_as_fees_rate.value;
            fees_accumulated_from_coin_seconds = coin_seconds_to_fees.to_uint64();

            share_type max_fees_allowed = fee_options.max_accumulated_fees_from_coin_seconds[payer_membership];
            dlog( "max_fees_payable=${p}, max_fees_allowed=${a}",
                  ("p",max_fees_payable_with_coin_seconds)("a",max_fees_allowed) );
            if( fees_accumulated_from_coin_seconds > max_fees_allowed ) // if accumulated too many coin seconds, truncate
            {
               fees_accumulated_from_coin_seconds = max_fees_allowed;
               coin_seconds_earned = fc::uint128_t( max_fees_allowed.value );
               coin_seconds_earned *= coin_seconds_as_fees_rate.value;
            }
            max_fees_payable_with_coin_seconds = std::min( fees_accumulated_from_coin_seconds, max_op_fee[o.which()] );
         }
      }
      dlog( "max_fees_payable=${p}, new coin_seconds_earned=${c}",
            ("p",max_fees_payable_with_coin_seconds)("c",coin_seconds_earned) );
   }

   void generic_evaluator::convert_fee()
   {
      if( !trx_state->skip_fee ) {
         if( fee_asset->get_id() != asset_id_type() )
         {
            db().modify(*fee_asset_dyn_data, [this](asset_dynamic_data_object& d) {
               d.accumulated_fees += fee_from_account.amount;
               d.fee_pool -= core_fee_paid;
            });
         }
      }
   }

   void generic_evaluator::pay_fee()
   { try {
      if( !trx_state->skip_fee ) {
         database& d = db();
         /// TODO: db().pay_fee( account_id, core_fee );
         d.modify(*fee_paying_account_statistics, [&](account_statistics_object& s)
         {
            s.pay_fee( core_fee_paid, d.get_global_properties().parameters.cashback_vesting_threshold );
         });
         pay_fee_with_coin_seconds();
      }
   } FC_CAPTURE_AND_RETHROW() }

   void generic_evaluator::pay_fee_with_coin_seconds()
   { try {
      if( !trx_state->skip_fee ) {
         database& d = db();
         // deduct fees from coin_seconds_earned
         if( fees_paid_with_coin_seconds > 0 )
         {
            d.modify(*fee_paying_account_statistics, [&](account_statistics_object& o) {
               fc::uint128_t coin_seconds_consumed( fees_paid_with_coin_seconds.value );
               coin_seconds_consumed *= coin_seconds_as_fees_rate.value;
               o.set_coin_seconds_earned( coin_seconds_earned - coin_seconds_consumed, d.head_block_time() );
            });
         }

      }
   } FC_CAPTURE_AND_RETHROW() }
} }
