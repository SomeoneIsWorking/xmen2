#ifndef X2_CONTROLLER_INSTANCE_H
#define X2_CONTROLLER_INSTANCE_H

#include <stdint.h>

/*
 * A DirectInput device belongs to one connected controller instance, not to
 * the inventory slot that happened to contain it when it was created. Slots
 * are reusable; the live instance GUID is not.
 */
typedef struct {
    unsigned char guid[16];
} X2ControllerInstance;

void x2_controller_instance_bind(X2ControllerInstance *instance,
                                 const unsigned char guid[16]);
int x2_controller_instance_matches(const X2ControllerInstance *instance,
                                   const unsigned char guid[16]);
int x2_controller_instance_resolve(const X2ControllerInstance *instance);

#endif /* X2_CONTROLLER_INSTANCE_H */
