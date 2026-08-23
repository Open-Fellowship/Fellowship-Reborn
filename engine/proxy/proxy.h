/* proxy.h - shared declarations for the engine proxy.
 *
 * Deliberately small. Anything that grows beyond "the proxy needs this" belongs
 * in a module of its own, so that the seam stays legible as functions move
 * across it.
 */

#ifndef OF_ENGINE_PROXY_H
#define OF_ENGINE_PROXY_H

/* The retail module, renamed so this DLL can take its place. */
#define OF_RETAIL_NAME   "Fellowship.orig.rfl"
#define OF_RETAIL_SUFFIX ".orig.rfl"

#define OF_ENGINE_LOG_NAME "fellowship_reborn_engine.log"
#define OF_ENGINE_INI_NAME "fellowship_reborn_engine.ini"

/* Every system this layer takes over is individually switchable, and that is not
 * a convenience - it is how a regression gets attributed.
 *
 * A reimplemented system can be faithful in every field a comparison can reach
 * and still change behaviour through something the comparison cannot see. When
 * that happens the only cheap question is "does it stop if I turn this one off",
 * and it has to be answerable without a rebuild, in one sitting, by whoever
 * noticed. Default is on; set the key to 0 in the ini beside the DLL to forward
 * to the retail module instead.
 *
 *     [engine]
 *     ObjectDefs=0
 */
int of_use_own(const char *key);

/* Non-zero once the retail module is loaded and every export bound. Every
 * forwarding export must consult this before dereferencing a pointer; a
 * reimplemented one need not, which is a useful way to see at a glance which is
 * which. */
int of_retail_ready(void);

void of_engine_log(const char *format, ...);

/* ObjectDef class ids, from the engine's own class table in Fellowship.rfl.
 *
 * These are not invented names. The table is documented in
 * documentation/OBJECT-MODEL.md and can be printed with
 * `python decomp/tools/classdump.py <name>`; the three predicates below were
 * matched byte for byte against the retail code, and the ids they compare are
 * exactly the classes these constants name. */
#define OF_CLASS_STATIC_LIGHT      0x1000B
#define OF_CLASS_DYNAMIC_LIGHT     0x1000C
#define OF_CLASS_STATIC_SPOT_LIGHT 0x1002C
#define OF_CLASS_DYNAMIC_SPOT_LIGHT 0x1002D
#define OF_CLASS_PORTAL            0x10108
#define OF_CLASS_MOVE_NODE_OBJECT  0x10140

#endif /* OF_ENGINE_PROXY_H */
