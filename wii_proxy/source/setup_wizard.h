/*============================================================================
 * wii_proxy/source/setup_wizard.h
 *
 * First-boot setup wizard for initial Wii configuration.
 * Prompts user to enter PC IP address and other settings.
 * Saves to sd:/apps/dsremote/proxy.cfg
 *==========================================================================*/
#ifndef WII_SETUP_WIZARD_H
#define WII_SETUP_WIZARD_H

#include <stdint.h>
#include "config_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Run the first-boot setup wizard.
 * Returns 0 if setup completed successfully, -1 on error.
 * Populates cfg with user-entered values.
 */
int setup_wizard_run(wii_config_t *cfg);

#ifdef __cplusplus
}
#endif
#endif /* WII_SETUP_WIZARD_H */
