/* dinput8_min.h: our own mouse, read the same way the game reads its own.
 *
 * WHY NOT THE SYSTEM CURSOR, AND WHY NOT RAW INPUT
 *
 * Two approaches were tried on the machine this runs on, and both failed for reasons worth
 * writing down, because both looked correct:
 *
 *   GetCursorPos.  The game acquires the mouse through DirectInput in EXCLUSIVE mode. A device
 *   acquired that way stops moving the system cursor and stops generating window messages, so
 *   GetCursorPos returns the same frozen point forever. The menu drew and could not be clicked.
 *
 *   Raw input.  WM_INPUT sits underneath DirectInput, which is why it looked like the answer.
 *   But only ONE window per raw input device class per PROCESS may be registered, and
 *   DirectInput8 registers the mouse itself inside the game's process. Its registration replaces
 *   ours and the messages never arrive.
 *
 * DirectInput devices do not have that problem: exclusive access by one application prevents
 * other applications acquiring EXCLUSIVELY, and nothing else. So this plugin opens its own mouse,
 * non-exclusive and background, and reads relative movement from it; the same hardware the game
 * is reading, through the same API, without taking anything away from it.
 *
 * It tries exclusive first. If the game does not hold the mouse exclusively, we get it, and the
 * game stops seeing the movement while the menu is open, which is the behaviour you actually
 * want from a menu. If it does, we fall back to sharing and say so in the log.
 *
 * NOTHING HERE COMES FROM AN SDK. The GUIDs are the published, stable DirectInput values and the
 * data format is a structure the API documents; `dinput8.lib` would supply both, but declaring
 * them keeps this plugin buildable with nothing but the Windows SDK, the same reasoning as
 * d3d8_min.h next door.
 */
#ifndef DEV_MENU_DINPUT8_MIN_H
#define DEV_MENU_DINPUT8_MIN_H

#include <windows.h>

#define DIRECTINPUT_VERSION_8 0x0800

/* ------------------------------------------------------------------------------- interfaces */

/* IDirectInput8 */
#define DI8_CREATEDEVICE          3

/* IDirectInputDevice8 */
#define DI8_DEV_ACQUIRE           7
#define DI8_DEV_UNACQUIRE         8
#define DI8_DEV_GETDEVICESTATE    9
#define DI8_DEV_SETDATAFORMAT     11
#define DI8_DEV_SETCOOPLEVEL      13
#define DI8_DEV_RELEASE           2

typedef HRESULT (WINAPI *direct_input8_create_t)(HINSTANCE instance, DWORD version,
                                                 const GUID *riid, void **out,
                                                 void *outer);

typedef HRESULT (STDMETHODCALLTYPE *di8_create_device_t)(void *self, const GUID *device,
                                                         void **out, void *outer);
typedef HRESULT (STDMETHODCALLTYPE *di8_acquire_t)(void *self);
typedef HRESULT (STDMETHODCALLTYPE *di8_get_state_t)(void *self, DWORD size, void *data);
typedef HRESULT (STDMETHODCALLTYPE *di8_set_format_t)(void *self, const void *format);
typedef HRESULT (STDMETHODCALLTYPE *di8_set_coop_t)(void *self, HWND window, DWORD flags);
typedef ULONG   (STDMETHODCALLTYPE *di8_release_t)(void *self);
typedef HRESULT (STDMETHODCALLTYPE *di8_get_data_t)(void *self, DWORD object_size, void *data,
                                                    DWORD *count, DWORD flags);

/* IDirectInputDevice8, the two ways a caller reads a mouse. */
#define DI8_DEV_GETDEVICEDATA     10

/* ------------------------------------------------------------------------------------ GUIDs */

static const GUID DEV_IID_IDirectInput8A =
    { 0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
static const GUID DEV_GUID_SysMouse =
    { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID DEV_GUID_XAxis =
    { 0xA36D02E0, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID DEV_GUID_YAxis =
    { 0xA36D02E1, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID DEV_GUID_ZAxis =
    { 0xA36D02E2, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID DEV_GUID_Button =
    { 0xA36D02F0, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

/* -------------------------------------------------------------------------- the data format */

typedef struct dev_object_format {
    const GUID *guid;
    DWORD       offset;
    DWORD       type;
    DWORD       flags;
} dev_object_format_t;

typedef struct dev_data_format {
    DWORD                size;
    DWORD                object_size;
    DWORD                flags;
    DWORD                data_size;
    DWORD                object_count;
    dev_object_format_t *objects;
} dev_data_format_t;

/* The layout DIMOUSESTATE2 has, declared rather than included. */
typedef struct dev_mouse_state {
    LONG x;
    LONG y;
    LONG z;
    BYTE buttons[8];
} dev_mouse_state_t;

#define DIDF_RELAXIS          0x00000002u
#define DIDFT_AXIS            0x00000003u
#define DIDFT_BUTTON          0x0000000Cu
#define DIDFT_ANYINSTANCE     0x00FFFF00u
#define DIDOI_ASPECTPOSITION  0x00000100u

#define DISCL_EXCLUSIVE       0x00000001u
#define DISCL_NONEXCLUSIVE    0x00000002u
#define DISCL_FOREGROUND      0x00000004u
#define DISCL_BACKGROUND      0x00000008u

#define DIERR_INPUTLOST       ((HRESULT)0x8007001EL)
#define DIERR_NOTACQUIRED     ((HRESULT)0x8007000CL)

#endif /* DEV_MENU_DINPUT8_MIN_H */
