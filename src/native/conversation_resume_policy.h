#ifndef X2_CONVERSATION_RESUME_POLICY_H
#define X2_CONVERSATION_RESUME_POLICY_H

#include "conversation_skip_policy.h"

#define CONVERSATION_RESUME_WAIT_SECONDS 10.0

typedef enum X2ConversationResumeState {
    X2_CONVERSATION_RESUME_IDLE,
    X2_CONVERSATION_RESUME_WAITING,
    X2_CONVERSATION_RESUME_SKIPPING
} X2ConversationResumeState;

typedef struct X2ConversationResumePolicy {
    X2ConversationResumeState state;
    double armed_at_s;
    int last_deterministic;
    unsigned armed;
    unsigned observations;
    unsigned advances;
    unsigned skipped;
    unsigned handed_back;
    unsigned retired;
    unsigned expired;
    unsigned manual_overrides;
} X2ConversationResumePolicy;

void x2_conversation_resume_policy_arm(X2ConversationResumePolicy *policy,
                                       double now_s);
void x2_conversation_resume_policy_observe(
    X2ConversationResumePolicy *policy, int visible, int ending,
    int owned_gap, ConversationSkipResponse response, double now_s);
int x2_conversation_resume_policy_expire(X2ConversationResumePolicy *policy,
                                         double now_s);
int x2_conversation_resume_policy_should_advance(
    const X2ConversationResumePolicy *policy);
int x2_conversation_resume_policy_active(
    const X2ConversationResumePolicy *policy);
void x2_conversation_resume_policy_note_advance(
    X2ConversationResumePolicy *policy);
void x2_conversation_resume_policy_manual_override(
    X2ConversationResumePolicy *policy);

#endif
