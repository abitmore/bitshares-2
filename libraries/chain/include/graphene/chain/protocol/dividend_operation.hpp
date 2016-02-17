#pragma once
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/memo.hpp>
namespace graphene {
	namespace chain {
		struct dividend_operation : public base_operation
		{
			struct fee_parameters_type {
				uint64_t fee = 200 * GRAPHENE_BLOCKCHAIN_PRECISION;
				uint64_t fee_per_shareholder = 0.1* GRAPHENE_BLOCKCHAIN_PRECISION;
				uint64_t fee_per_shareholder_show = 0.5* GRAPHENE_BLOCKCHAIN_PRECISION;
				uint32_t price_per_kbyte = 10 * GRAPHENE_BLOCKCHAIN_PRECISION; /// only required for large memos.
			};
			asset fee;
			/// dividend isser
			account_id_type  isser;
			/// The sharea records
			//map<account_id_type,share_type> to;
			// which asset 
			asset_id_type shares_asset;
			// which asset to didivlde 
			asset_id_type dividend_asset;
			// have min_shares to can get dividle
			uint16_t min_shares;
			uint16_t value_per_shares;
			uint64_t block_no;
			string describtion;
			extensions_type   extensions;
			/*dividend_operation(account_id_type _isser, asset_id_type _shares_asset, asset_id_type _dividend_asset, uint16_t _min_shares, uint16_t _value_per_shares, uint64_t _block_no) :
				isser(_isser), shares_asset(_shares_asset), dividend_asset(_dividend_asset), min_shares(_min_shares), value_per_shares(_value_per_shares), block_no(_block_no){};*/
			void get_to();

			account_id_type fee_payer()const { return isser; }
			void            validate()const;
			share_type      calculate_fee(const fee_parameters_type& k)const;
		};
	}
}
FC_REFLECT(graphene::chain::dividend_operation::fee_parameters_type, (fee)(price_per_kbyte))
FC_REFLECT(graphene::chain::dividend_operation, (fee)(isser)(shares_asset)(dividend_asset)(min_shares)(value_per_shares)(block_no)(describtion)(extensions))