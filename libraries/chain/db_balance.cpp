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

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/witness_object.hpp>

namespace graphene { namespace chain {

asset database::get_balance(account_id_type owner, asset_id_type asset_id) const
{
   auto& index = get_index_type<account_balance_index>().indices().get<by_balance>();
   auto itr = index.find(boost::make_tuple(owner, asset_id));
   if( itr == index.end() )
      return asset(0, asset_id);
   return itr->get_balance();
}

asset database::get_balance(const account_object& owner, const asset_object& asset_obj) const
{
   return get_balance(owner.get_id(), asset_obj.get_id());
}

string database::to_pretty_string( const asset& a )const
{
   return a.asset_id(*this).amount_to_pretty_string(a.amount);
}

void database::adjust_balance(account_id_type account, asset delta )
{ try {
   if( delta.amount == 0 )
      return;

   auto& index = get_index_type<account_balance_index>().indices().get<by_balance>();
   auto itr = index.find(boost::make_tuple(account, delta.asset_id));
   if(itr == index.end())
   {
      FC_ASSERT( delta.amount > 0, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", 
                 ("a",account(*this).name)
                 ("b",to_pretty_string(asset(0,delta.asset_id)))
                 ("r",to_pretty_string(-delta)));
      create<account_balance_object>([account,&delta](account_balance_object& b) {
         b.owner = account;
         b.asset_type = delta.asset_id;
         b.balance = delta.amount.value;
      });
   } else {
      if( delta.amount < 0 )
         FC_ASSERT( itr->get_balance() >= -delta, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}", ("a",account(*this).name)("b",to_pretty_string(itr->get_balance()))("r",to_pretty_string(-delta)));
      const asset original_balance = itr->get_balance();
      modify(*itr, [delta](account_balance_object& b) {
         b.adjust_balance(delta);
      });
      // Update coin_seconds_earned and etc
      if( head_block_time() > HARDFORK_FREE_TRX_TIME )
      {
         // TODO how to deal with smart coins?
         if( delta.asset_id == asset_id_type() )
         {
            modify( account(*this).statistics(*this), [&](account_statistics_object& s) {
               //TODO will coin_seconds_earned expire? seems hard to calculate
               s.update_coin_seconds_earned( original_balance, head_block_time() );
            });
         }
      }
   }

} FC_CAPTURE_AND_RETHROW( (account)(delta) ) }

optional< vesting_balance_id_type > database::deposit_lazy_vesting(
   const optional< vesting_balance_id_type >& ovbid,
   share_type amount, uint32_t req_vesting_seconds,
   account_id_type req_owner,
   bool require_vesting )
{
   if( amount == 0 )
      return optional< vesting_balance_id_type >();

   fc::time_point_sec now = head_block_time();

   while( true )
   {
      if( !ovbid.valid() )
         break;
      const vesting_balance_object& vbo = (*ovbid)(*this);
      if( vbo.owner != req_owner )
         break;
      if( vbo.policy.which() != vesting_policy::tag< cdd_vesting_policy >::value )
         break;
      if( vbo.policy.get< cdd_vesting_policy >().vesting_seconds != req_vesting_seconds )
         break;
      modify( vbo, [&]( vesting_balance_object& _vbo )
      {
         if( require_vesting )
            _vbo.deposit(now, amount);
         else
            _vbo.deposit_vested(now, amount);
      } );
      return optional< vesting_balance_id_type >();
   }

   const vesting_balance_object& vbo = create< vesting_balance_object >( [&]( vesting_balance_object& _vbo )
   {
      _vbo.owner = req_owner;
      _vbo.balance = amount;

      cdd_vesting_policy policy;
      policy.vesting_seconds = req_vesting_seconds;
      policy.coin_seconds_earned = require_vesting ? 0 : amount.value * policy.vesting_seconds;
      policy.coin_seconds_earned_last_update = now;

      _vbo.policy = policy;
   } );

   return vbo.id;
}

void database::deposit_cashback(const account_object& acct, share_type amount, bool require_vesting)
{
   // If we don't have a VBO, or if it has the wrong maturity
   // due to a policy change, cut it loose.

   if( amount == 0 )
      return;

   if( acct.get_id() == GRAPHENE_COMMITTEE_ACCOUNT || acct.get_id() == GRAPHENE_WITNESS_ACCOUNT ||
       acct.get_id() == GRAPHENE_RELAXED_COMMITTEE_ACCOUNT || acct.get_id() == GRAPHENE_NULL_ACCOUNT ||
       acct.get_id() == GRAPHENE_TEMP_ACCOUNT )
   {
      // The blockchain's accounts do not get cashback; it simply goes to the reserve pool.
      modify(get(asset_id_type()).dynamic_asset_data_id(*this), [amount](asset_dynamic_data_object& d) {
         d.current_supply -= amount;
      });
      return;
   }

   optional< vesting_balance_id_type > new_vbid = deposit_lazy_vesting(
      acct.cashback_vb,
      amount,
      get_global_properties().parameters.cashback_vesting_period_seconds,
      acct.id,
      require_vesting );

   if( new_vbid.valid() )
   {
      modify( acct, [&]( account_object& _acct )
      {
         _acct.cashback_vb = *new_vbid;
      } );
   }

   return;
}

void database::deposit_witness_pay(const witness_object& wit, share_type amount)
{
   if( amount == 0 )
      return;

   optional< vesting_balance_id_type > new_vbid = deposit_lazy_vesting(
      wit.pay_vb,
      amount,
      get_global_properties().parameters.witness_pay_vesting_seconds,
      wit.witness_account,
      true );

   if( new_vbid.valid() )
   {
      modify( wit, [&]( witness_object& _wit )
      {
         _wit.pay_vb = *new_vbid;
      } );
   }

   return;
}

} }
