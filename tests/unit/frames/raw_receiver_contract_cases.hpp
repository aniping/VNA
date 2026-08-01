TEST(FrameContractTest, PreservesMultipleSourceStatesAndResponsePorts) {
    RawReceiverPayload payload{
        .portCount = 2,
        .sourceStates = {
            {.sourcePort = 1,
             .samples = {
                 {.reference = {1.0, 0.0},
                  .responses = {{0.9, 0.1}, {0.01, -0.02}}},
                 {.reference = {1.0, 0.0},
                  .responses = {{0.8, 0.2}, {0.02, -0.01}}},
             }},
            {.sourcePort = 2,
             .samples = {
                 {.reference = {1.0, 0.0},
                  .responses = {{-0.01, 0.02}, {0.9, -0.1}}},
                 {.reference = {1.0, 0.0},
                  .responses = {{-0.02, 0.01}, {0.8, -0.2}}},
             }},
        },
    };
    auto duplicate = payload;
    duplicate.sourceStates[1].sourcePort = 1;

    const auto result =
        makeRawReceiverFrame(validContext(), validAxis(), std::move(payload));
    const auto rejected = makeRawReceiverFrame(
        validContext(), validAxis(), std::move(duplicate));

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().payload.sourceStates.size(), 2U);
    EXPECT_DOUBLE_EQ(
        result.value().payload.sourceStates[1].samples[0].responses[1].real,
        0.9);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, FrameErrorCode::DuplicateSourcePort);
}
