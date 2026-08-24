#ifndef X2_CONVERSATION_RESUME_H
#define X2_CONVERSATION_RESUME_H

#include <stdint.h>

struct CPU;

void x2_conversation_resume_continue_started(void);
void x2_conversation_resume_cancel_pending(void);
void x2_conversation_resume_map_return(int succeeded);
void x2_conversation_resume_observe(struct CPU *cpu, uint32_t self,
                                    uint8_t flags);
int x2_conversation_resume_gate_open(void);
int x2_conversation_resume_should_advance(struct CPU *cpu, uint32_t self);
int x2_conversation_resume_sequence_active(void);
void x2_conversation_resume_manual_override(void);
void x2_conversation_resume_report(void);

#endif
