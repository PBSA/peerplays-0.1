#include <boost/test/unit_test.hpp>

#include "../common/database_fixture.hpp"
#include <graphene/chain/contract_object.hpp>
#include <graphene/chain/contract_evaluator.hpp>
#include <evm_result.hpp>

#include <fc/filesystem.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace vms::evm;

/*
    pragma solidity >=0.4.22 <0.6.0;
    contract solidityAdd {
        uint a = 26;
        function add() public {
            a = a + 1;
        }
        function() external payable {}
        constructor () public payable {}
    }
*/
std::string solidityAddCode = "6080604052601a60005560898060166000396000f3fe6080604052600436106039576000357c0100000000000000000000000000000000000000000000000000000000900480634f2be91f14603b575b005b348015604657600080fd5b50604d604f565b005b60016000540160008190555056fea165627a7a72305820ca7373d84858554566f0336168b09e3e765186ac38e4cf18cc584bcec4bd7fe20029";


BOOST_FIXTURE_TEST_SUITE( split_transfer_gas_tests, database_fixture )

const uint64_t transfer_amount = 50000;
const uint64_t gas_limit_amount = 1000000;
const uint64_t custom_asset_transfer_amount = 100000000; // do not set less then 1000000 ( `not_enough_asset_for_fee_test` and `not_enough_asset_for_transfer_test` will fail this way )
const uint64_t core_asset_transfer_amount = 1000000000;
const uint64_t fee_pool_amount = 1000000;

inline const account_statistics_object& test_contract_deploy( database& db, asset_id_type first_asset, asset_id_type second_asset ) {
    transaction_evaluation_state context(&db);
    asset_fund_fee_pool_operation op_fund_fee_pool;
    op_fund_fee_pool.asset_id = first_asset;
    op_fund_fee_pool.from_account = account_id_type(0);
    op_fund_fee_pool.amount = fee_pool_amount;

    db.apply_operation( context, op_fund_fee_pool );

    contract_operation contract_op;
    contract_op.vm_type = vm_types::EVM;
    contract_op.registrar = account_id_type(5);
    contract_op.fee = asset(0, first_asset);
    contract_op.data = fc::raw::unsigned_pack( eth_op{ contract_op.registrar, optional<contract_id_type>(), std::set<uint64_t>(), second_asset, transfer_amount, first_asset, 1, gas_limit_amount, solidityAddCode } );

    db._evaluating_from_block = true;
    db.apply_operation( context, contract_op );
    db._evaluating_from_block = false;

    const simple_index<account_statistics_object>& statistics_index = db.get_index_type<simple_index<account_statistics_object>>();
    auto iter = statistics_index.begin();
    for(size_t i = 0; i < 5; i++)
        iter++;
    return *iter;
}

inline share_type get_gas_used( database& db, result_contract_id_type result_id ) {
    auto raw_res = db.db_res.get_results( std::string( (object_id_type)result_id ) );
    auto res = fc::raw::unpack< vms::evm::evm_result >( *raw_res );
    return res.exec_result.gasUsed.convert_to<share_type>();
}

BOOST_AUTO_TEST_CASE( fee_test ){
    asset_object temp_asset = create_user_issued_asset("ETB");
    issue_uia( account_id_type(5), temp_asset.amount( custom_asset_transfer_amount ) );
    transfer( account_id_type(0), account_id_type(5), asset( core_asset_transfer_amount, asset_id_type() ) );

    const auto& a = test_contract_deploy( db, asset_id_type(), asset_id_type(1) );
    auto gas_used = get_gas_used( db, result_contract_id_type() );

    BOOST_CHECK( a.pending_fees + a.pending_vested_fees == gas_used );
    BOOST_CHECK( db.get_balance(contract_id_type(), asset_id_type()).amount.value == 0 );
    BOOST_CHECK( db.get_balance(contract_id_type(), asset_id_type(1)).amount.value == transfer_amount );
    BOOST_CHECK( db.get_balance(account_id_type(5), asset_id_type()).amount.value == core_asset_transfer_amount - gas_used );
    BOOST_CHECK( db.get_balance(account_id_type(5), asset_id_type(1)).amount.value == custom_asset_transfer_amount - transfer_amount );
}

BOOST_AUTO_TEST_CASE( not_CORE_fee_test ){
    asset_object temp_asset = create_user_issued_asset("ETB");
    issue_uia( account_id_type(5), temp_asset.amount( custom_asset_transfer_amount ) );
    transfer( account_id_type(0), account_id_type(5), asset(core_asset_transfer_amount, asset_id_type() ) );

    const auto& a = test_contract_deploy( db, asset_id_type(1), asset_id_type() );
    auto gas_used = get_gas_used( db, result_contract_id_type() );

    BOOST_CHECK( a.pending_fees + a.pending_vested_fees == gas_used );
    BOOST_CHECK( db.get_balance(contract_id_type(), asset_id_type(1)).amount.value == 0 );
    BOOST_CHECK( db.get_balance(contract_id_type(), asset_id_type()).amount.value == transfer_amount );
    BOOST_CHECK( db.get_balance(account_id_type(5), asset_id_type()).amount.value == core_asset_transfer_amount - transfer_amount );
    BOOST_CHECK( db.get_balance(account_id_type(5), asset_id_type(1)).amount.value == custom_asset_transfer_amount - gas_used );
}

BOOST_AUTO_TEST_CASE( mixed_assets_test ){
    asset_object temp_asset = create_user_issued_asset("ETB");
    issue_uia( account_id_type(5), temp_asset.amount( custom_asset_transfer_amount ) );
    transfer(account_id_type(0),account_id_type(5),asset(core_asset_transfer_amount, asset_id_type()));

    const auto& a = test_contract_deploy( db, asset_id_type(1), asset_id_type() );
    auto gas_deploy = get_gas_used( db, result_contract_id_type() );    

    BOOST_CHECK(a.pending_fees + a.pending_vested_fees == gas_deploy);

    transaction_evaluation_state context(&db);
    contract_operation contract_op;
    contract_op.vm_type = vm_types::EVM;
    contract_op.registrar = account_id_type(5);
    contract_op.fee = asset(0, asset_id_type());
    contract_op.data = fc::raw::unsigned_pack( eth_op{ contract_op.registrar, contract_id_type(), std::set<uint64_t>(), asset_id_type(1), transfer_amount, asset_id_type(), 1, gas_limit_amount } );

    db._evaluating_from_block = true;
    db.apply_operation( context, contract_op );
    db._evaluating_from_block = false;

    auto gas_call =  get_gas_used( db, result_contract_id_type(1) );

    BOOST_CHECK( db.get_balance(contract_id_type(), asset_id_type()).amount.value == transfer_amount );
    BOOST_CHECK( db.get_balance(contract_id_type(), asset_id_type(1)).amount.value == transfer_amount );
    BOOST_CHECK( db.get_balance(account_id_type(5), asset_id_type()).amount.value == core_asset_transfer_amount - transfer_amount - gas_call );
    BOOST_CHECK( db.get_balance(account_id_type(5), asset_id_type(1)).amount.value == custom_asset_transfer_amount - transfer_amount - gas_deploy );
}

inline void not_enough_cash_call( database_fixture& df, asset_id_type transfer, asset_id_type gas ){
    asset_object temp_asset = df.create_user_issued_asset("ETB");
    df.issue_uia( account_id_type(5), temp_asset.amount( custom_asset_transfer_amount / 100000 ) );
    df.transfer( account_id_type(0), account_id_type(5), asset( core_asset_transfer_amount, asset_id_type() ) );

    transaction_evaluation_state context(&df.db);
    asset_fund_fee_pool_operation op_fund_fee_pool;
    op_fund_fee_pool.asset_id = gas;
    op_fund_fee_pool.from_account = account_id_type(0);
    op_fund_fee_pool.amount = fee_pool_amount;

    df.db.apply_operation( context, op_fund_fee_pool );

    contract_operation contract_op;
    contract_op.vm_type = vm_types::EVM;
    contract_op.registrar = account_id_type(5);
    contract_op.fee = asset(0, gas);
    contract_op.data = fc::raw::unsigned_pack( eth_op{ contract_op.registrar, optional<contract_id_type>(), std::set<uint64_t>(), transfer, transfer_amount, gas, 1, gas_limit_amount, solidityAddCode } );

    df.db._evaluating_from_block = true;
    GRAPHENE_REQUIRE_THROW( df.db.apply_operation( context, contract_op ), fc::exception );
    df.db._evaluating_from_block = false;
}

BOOST_AUTO_TEST_CASE( not_enough_asset_for_fee_test ){
    not_enough_cash_call( *this, asset_id_type(), asset_id_type(1) );
}

BOOST_AUTO_TEST_CASE( not_enough_asset_for_transfer_test ){
    not_enough_cash_call( *this, asset_id_type(1), asset_id_type() );
}

BOOST_AUTO_TEST_SUITE_END()