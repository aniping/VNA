vna_add_test(
    vna_cross_layer_contract_tests
    cross_layer_contract_tests.cpp
    vna::runtime
    vna::board_mock
)

vna_add_test(
    vna_a_only_snapshot_contract_tests
    a_only_snapshot_contract_tests.cpp
    vna::runtime
    vna::board_mock
)
