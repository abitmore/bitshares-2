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
#include <graphene/chain/protocol/committee_member.hpp>

namespace graphene { namespace chain {

void committee_member_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

void committee_member_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   if( new_url.valid() )
      FC_ASSERT(new_url->size() < GRAPHENE_MAX_URL_LENGTH );
}

void committee_member_update_global_parameters_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   new_parameters.validate();
}

void committee_member_update_core_asset_operation::validate() const
{
   // Validate fee field
   FC_ASSERT( fee.amount >= 0 );
   // Validate market_fee_percent and max_market_fee in new_options
   FC_ASSERT( new_options.market_fee_percent <= GRAPHENE_100_PERCENT );
   FC_ASSERT( new_options.max_market_fee >= 0 && new_options.max_market_fee <= GRAPHENE_MAX_SHARE_SUPPLY );
   // extensions::transfer_fee_mode in new_options doesn't need to be validated.
   // Other fields in new_options will be ignored, so don't need to be validated.
}

} } // graphene::chain
