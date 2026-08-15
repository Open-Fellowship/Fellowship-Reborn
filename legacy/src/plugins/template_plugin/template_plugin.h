/* template_plugin.h: the smallest thing that satisfies the loader contract.
 *
 * Copy this directory to start a real plugin. The shape to keep is the split between dll_main.c,
 * which is the entry point and nothing else, and this file, which is the behaviour. Every plugin
 * in this tree follows it, so a reader always knows which file to open first.
 */
#ifndef TEMPLATE_PLUGIN_H
#define TEMPLATE_PLUGIN_H

/* Called once, from the loader, outside the loader lock, with the host image fully mapped. */
void template_plugin_install(void);

#endif /* TEMPLATE_PLUGIN_H */
