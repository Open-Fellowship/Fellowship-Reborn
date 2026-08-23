/* template_plugin.h: the smallest thing that satisfies the loader contract.
 *
 * Copy this directory to start a real plugin. Keep the split: dll_main.c is the entry point and
 * nothing else, this file is the behaviour. Every plugin here follows it. See README.md.
 */
#ifndef TEMPLATE_PLUGIN_H
#define TEMPLATE_PLUGIN_H

/* Called once, from the loader, outside the loader lock, with the host image fully mapped. */
void template_plugin_install(void);

#endif /* TEMPLATE_PLUGIN_H */
