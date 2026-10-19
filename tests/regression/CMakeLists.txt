# Reuse production sources without changing the application's build or fetching test frameworks.
add_executable(anodrop_regression
    contracts.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/Logger.cpp
    handshake_callbacks.cpp
    network_workflows.cpp
    ${PROJECT_SOURCE_DIR}/chat/src/DhtChat.cpp
    ${PROJECT_SOURCE_DIR}/network/src/HandshakeCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/network/src/MessageReceiver.cpp
    ${PROJECT_SOURCE_DIR}/network/src/OutboxPublisher.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/PartitionedThreadPool.cpp
    ${PROJECT_SOURCE_DIR}/ui/src/MessageStatusResolver.cpp
    ${PROJECT_SOURCE_DIR}/lobby/src/Lobby.cpp
    ${PROJECT_SOURCE_DIR}/lobby/src/LobbyRegistry.cpp
    ${PROJECT_SOURCE_DIR}/lobby/src/LobbyDataSerializer.cpp
    ${PROJECT_SOURCE_DIR}/storage/src/LobbyStore.cpp
    ${PROJECT_SOURCE_DIR}/crypto/src/CryptoUtils.cpp
    ${PROJECT_SOURCE_DIR}/crypto/src/HandshakeKeyService.cpp
    ${PROJECT_SOURCE_DIR}/crypto/src/HandshakePayloadCodec.cpp
    ${PROJECT_SOURCE_DIR}/lobby/src/HandshakeStateEngine.cpp
    ${PROJECT_SOURCE_DIR}/lobby/src/HandshakePreparation.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/IoWorker.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/TimeUtils.cpp
    ${PROJECT_SOURCE_DIR}/core/src/Config.cpp)
target_include_directories(anodrop_regression PRIVATE $<TARGET_PROPERTY:AnoDrop,INCLUDE_DIRECTORIES>)
target_link_libraries(anodrop_regression PRIVATE $<TARGET_PROPERTY:AnoDrop,LINK_LIBRARIES>)
foreach(case reentrant_send transaction_contracts outgoing_commands network_workflows domain_commands statuses resync ack_gaps sequence_dedup json_roundtrip snapshot_schema persistence compaction compaction_pages password heartbeat terminal disk_reader checker_mutations log_analysis handshake_state handshake_rejections handshake_late_ready handshake_late_pending handshake_sequence_completion imported_config delivery_observation)
    add_test(NAME regression.${case} COMMAND anodrop_regression ${case})
    set_tests_properties(regression.${case} PROPERTIES TIMEOUT 120 LABELS "regression;offline")
endforeach()

if(UNIX)
    add_executable(anodrop_e2e e2e.cpp
        ${PROJECT_SOURCE_DIR}/lobby/src/Lobby.cpp
        ${PROJECT_SOURCE_DIR}/crypto/src/CryptoUtils.cpp
        ${PROJECT_SOURCE_DIR}/lobby/src/LobbyDataSerializer.cpp
        ${PROJECT_SOURCE_DIR}/utils/src/TimeUtils.cpp)
    target_include_directories(anodrop_e2e PRIVATE $<TARGET_PROPERTY:AnoDrop,INCLUDE_DIRECTORIES>)
    target_link_libraries(anodrop_e2e PRIVATE $<TARGET_PROPERTY:AnoDrop,LINK_LIBRARIES> util)
    option(ANODROP_ENABLE_DHT_E2E "Register long public-DHT regression tests in CTest" OFF)
    if(ANODROP_ENABLE_DHT_E2E)
        add_test(NAME regression.dht_e2e COMMAND anodrop_e2e
            --binary $<TARGET_FILE:AnoDrop> --output ${CMAKE_CURRENT_BINARY_DIR}/artifacts --extended)
        set_tests_properties(regression.dht_e2e PROPERTIES
            TIMEOUT 18000 RUN_SERIAL TRUE LABELS "regression;dht;e2e")
    endif()
endif()

# Isolated runtime failure injection; no network session or user data.
add_executable(anodrop_cpp_runtime cpp_runtime.cpp
    ${PROJECT_SOURCE_DIR}/lobby/src/Lobby.cpp
    ${PROJECT_SOURCE_DIR}/ui/src/ChatViewModel.cpp
    ${PROJECT_SOURCE_DIR}/ui/src/MessageStatusResolver.cpp
    ${PROJECT_SOURCE_DIR}/chat/src/ChatResyncer.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/IoWorker.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/TimeUtils.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/PartitionedThreadPool.cpp)
target_include_directories(anodrop_cpp_runtime PRIVATE $<TARGET_PROPERTY:AnoDrop,INCLUDE_DIRECTORIES>)
target_link_libraries(anodrop_cpp_runtime PRIVATE $<TARGET_PROPERTY:AnoDrop,LINK_LIBRARIES>)
foreach(case pool_capacity atomic_update pool_shutdown io_lifecycle pool_failure lobby_failure read_retry)
    add_test(NAME regression.${case} COMMAND anodrop_cpp_runtime ${case})
    set_tests_properties(regression.${case} PROPERTIES TIMEOUT 10 LABELS "regression;offline")
endforeach()

# Real operation registries, exercised without opening a libtorrent session.
add_executable(anodrop_network_runtime network_runtime.cpp
    ${PROJECT_SOURCE_DIR}/network/src/DhtManager.cpp
    ${PROJECT_SOURCE_DIR}/network/src/Dropper.cpp
    ${PROJECT_SOURCE_DIR}/network/src/AlertListener.cpp
    ${PROJECT_SOURCE_DIR}/network/src/KillSwitch.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/NetworkUtils.cpp
    ${PROJECT_SOURCE_DIR}/utils/src/TimeUtils.cpp
    ${PROJECT_SOURCE_DIR}/crypto/src/CryptoUtils.cpp
    ${PROJECT_SOURCE_DIR}/crypto/src/HandshakeKeyService.cpp
    ${PROJECT_SOURCE_DIR}/crypto/src/HandshakePayloadCodec.cpp
    ${PROJECT_SOURCE_DIR}/core/src/Config.cpp)
target_include_directories(anodrop_network_runtime PRIVATE $<TARGET_PROPERTY:AnoDrop,INCLUDE_DIRECTORIES>)
target_link_libraries(anodrop_network_runtime PRIVATE $<TARGET_PROPERTY:AnoDrop,LINK_LIBRARIES>)
add_test(NAME regression.registry_rollback COMMAND anodrop_network_runtime)
set_tests_properties(regression.registry_rollback PROPERTIES TIMEOUT 15 LABELS "regression;offline")
set_tests_properties(regression.reentrant_send PROPERTIES TIMEOUT 15)
