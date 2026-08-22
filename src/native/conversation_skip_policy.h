#ifndef X2_CONVERSATION_SKIP_POLICY_H
#define X2_CONVERSATION_SKIP_POLICY_H

typedef enum ConversationSkipResponse {
    CONVERSATION_SKIP_RESPONSE_WAITING,
    CONVERSATION_SKIP_RESPONSE_DETERMINISTIC,
    CONVERSATION_SKIP_RESPONSE_CHOICE,
    CONVERSATION_SKIP_RESPONSE_UNREADABLE
} ConversationSkipResponse;

typedef enum ConversationSkipDecision {
    CONVERSATION_SKIP_NONE,
    CONVERSATION_SKIP_ADVANCE,
    CONVERSATION_SKIP_BLOCKED
} ConversationSkipDecision;

typedef struct ConversationSkipPolicy {
    unsigned active;
    unsigned requests;
    unsigned advances;
    unsigned blocked;
    unsigned ignored;
} ConversationSkipPolicy;

ConversationSkipDecision conversation_skip_policy_update(
    ConversationSkipPolicy *policy, int visible, int authored,
    int continuation_locked, int skip_pressed,
    ConversationSkipResponse response);

#endif
