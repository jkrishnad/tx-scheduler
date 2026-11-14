#pragma once
#include <cstddef>
#include <cstdint>

struct ProtocolVersions {
    std::uint64_t handshake;
    std::uint64_t scheduler_bindings;
    std::uint64_t shaq;
    std::uint64_t rts_alloc;
};

struct ClientLogon {
    std::size_t worker_count;
    std::size_t check_worker_count;
    std::size_t allocator_size;
    std::size_t allocator_handles;
    std::size_t tpu_to_pack_capacity;
    std::size_t progress_tracker_capacity;
    std::size_t pack_to_worker_capacity;
    std::size_t worker_to_pack_capacity;
    std::size_t pack_to_check_worker_capacity;
    std::size_t check_worker_to_pack_capacity;
    std::uint64_t flags;
};

struct SharableTransactionRegion {
    std::size_t offset;
    std::uint32_t length;
};

struct SharableTransactionBatchRegion {
    std::uint8_t num_transactions;
    std::size_t transactions_offset;
};

struct ExecutionResponseRegion {
    std::uint8_t num_transaction_responses;
    std::size_t transaction_responses_offset;
};

struct TpuToPackMessage {
    SharableTransactionRegion transaction;
    std::uint8_t flags;
    std::uint8_t src_addr[16];
};

struct ProgressMessage {
    std::uint8_t leader_state;
    std::uint8_t current_slot_progress;
    std::uint64_t epoch;
    std::uint64_t current_slot;
    std::uint64_t next_leader_slot;
    std::uint64_t leader_range_end;
    std::uint64_t remaining_cost_units;
    std::uint64_t remaining_allocated_accounts_data_size;
    std::uint8_t latest_blockhash[32];
    std::uint16_t target_bank_time_ms;
};

struct PackToExecutionWorkerMessage {
    std::uint16_t flags;
    std::uint64_t max_working_slot;
    SharableTransactionBatchRegion batch;
};

struct ExecutionWorkerToPackMessage {
    SharableTransactionBatchRegion batch;
    std::uint8_t processed_code;
    ExecutionResponseRegion responses;
};

struct ExecutionResponse {
    std::uint64_t execution_slot;
    std::uint8_t not_included_reason;
    std::uint64_t cost_units;
    std::uint64_t fee_payer_balance;
};

// ProgressMessage::leader_state values
inline constexpr std::uint8_t NOT_LEADER = 0;
inline constexpr std::uint8_t LEADER_STARTING = 1;
inline constexpr std::uint8_t LEADER_READY = 2;

// A batch with more transactions than this is rejected as INVALID.
inline constexpr std::size_t MAX_TRANSACTIONS_PER_MESSAGE = 64;

// TpuToPackMessage::flags bits
namespace tpu_message_flags {
inline constexpr std::uint8_t IS_SIMPLE_VOTE = 1 << 0;
inline constexpr std::uint8_t FORWARDED = 1 << 1;
inline constexpr std::uint8_t FROM_STAKED_NODE = 1 << 2;
} // namespace tpu_message_flags

// PackToExecutionWorkerMessage::flags bits
namespace execution_message_flags {
inline constexpr std::uint16_t DROP_ON_FAILURE = 1 << 0;
inline constexpr std::uint16_t ALL_OR_NOTHING = 1 << 1;
} // namespace execution_message_flags

// ExecutionWorkerToPackMessage::processed_code values
namespace processed_codes {
inline constexpr std::uint8_t PROCESSED = 0;
inline constexpr std::uint8_t INVALID = 1;
inline constexpr std::uint8_t MAX_WORKING_SLOT_EXCEEDED = 2;
} // namespace processed_codes

// ExecutionResponse::not_included_reason values (subset used here)
namespace not_included_reasons {
inline constexpr std::uint8_t NONE = 0;
inline constexpr std::uint8_t ACCOUNT_IN_USE = 64;
} // namespace not_included_reasons
