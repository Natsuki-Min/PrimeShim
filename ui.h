#pragma once
//#include "common.h"

/**
 * @brief List of available keycodes.
 * @details Keycodes starting with `KEY_PRIME_` are extended keycodes exclusive to HP Prime G1 (EA656).
 */


/**
 * @brief UI event types.
 */
typedef enum ui_event_type_e  {
	/**
	 * @brief Invalid/cleared.
	 */
	UI_EVENT_TYPE_INVALID = 0,
	/**
	 * @brief Beginning of touch/pen down event.
	 */
	UI_EVENT_TYPE_TOUCH_BEGIN = 1,
	/**
	 * @brief Touch/pen move event.
	 */
	UI_EVENT_TYPE_TOUCH_MOVE = 2,
	/**
	 * @brief End of touch/pen up event.
	 */
	UI_EVENT_TYPE_TOUCH_END = 8,

	UI_EVENT_TYPE_TICK = 0xf,

	UI_EVENT_TYPE_TICK_2 = 0x100010,
	
	UI_EVENT_TYPE_REDRAW = 0x4000,
	/**
	 * @brief Key(s) pressed.
	 */
	UI_EVENT_TYPE_KEY = 16,
	/**
	 * @brief Key(s) released.
	 * @details Available on S3C and TCC boards.
	 */
	UI_EVENT_TYPE_KEY_UP = 0x100000,
}ui_event_type_e;


/**
 * @brief Multipress/multitouch event.
 * @details This is a simplified version of the main UI event struct, that only contains the necessary fields to
 * represent a multitouch or a key-press event. Used on Prime G1.
 */
typedef struct UIMultipressEvent {
	/**
	 * @brief Type of event.
	 * @see ui_event_type_e List of event types.
	 */
	unsigned int type;
	/**
	 * @brief Finger ID of a touch event.
	 */
	unsigned short finger_id;
	union {
		struct {
			/**
			 * @brief Keycode for the first pressed key.
			 */
			unsigned short key_code0;
			/**
			 * @brief Keycode for the second pressed key (maybe unused).
			 */
			unsigned short key_code1;
		};
		struct {
			/**
			 * @brief The X coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_x;
			/**
			 * @brief The Y coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_y;
		};
	};
	/**
	 * @brief Unknown. Maybe unused and probably padding.
	 */
	unsigned short unk_0xb;
}UIMultipressEvent;

/**
 * @brief Structure for low level UI events (Prime G1 extension).
 * @details Define `MUTEKI_HAS_PRIME_UI_EVENT` as 1 to make this the underlying type of ui_event_t.
 */
typedef struct ui_event_prime_s {
	/**
	 * @brief Event recipient.
	 * @details If set to `NULL`, the event is a broadcast event (e.g. input event). Otherwise, the
	 * widget's ui_component_t::on_event callback will be called with this event.
	 */
	void** recipient; // 0-4
	/**
	 * @brief The type of event (0x10 being key event)
	 * @see ui_event_type_e List of event types.
	 */
	ui_event_type_e event_type; // 4-8 16: key (?).
	union {
		struct {
			/**
			 * @brief Keycode for the first pressed key.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_KEY.
			 */
			unsigned short key_code0; // 8-10
			/**
			 * @brief Keycode for the second pressed key.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_KEY.
			 * @note Depending on the exact keys pressed simultaneously, this is not always accurate. Moreover,
			 * some devices may lack support of simultaneous key presses.
			 */
			unsigned short key_code1; // 10-12 sometimes set when 2 keys are pressed simultaneously. Does not always work.
		};
		struct {
			/**
			 * @brief The X coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_x;
			/**
			 * @brief The Y coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_y;
		};
	};
	/**
	 * @brief Unknown.
	 * @details Set along with a ::KEY_USB_INSERTION event. Seems to point to some data. Exact purpose unknown.
	 */
	void* usb_data; // 12-16 pointer that only shows up on USB insertion event.
	/**
	 * @brief Unknown.
	 * @details Maybe used on event types other than touch and key press.
	 */
	void* unk16; // 16-20 sometimes a pointer especially on unstable USB connection? junk data?
	/**
	 * @brief Unknown.
	 * @details Seems to be always 0, although ClearEvent() explicitly sets this to 0. Maybe used on event types other
	 * than touch and key press.
	 */
	void* unk20; // 20-24 seems to be always 0. Unused?
	/**
	 * @brief Number of valid multipress events available for processing.
	 */
	unsigned short available_multipress_events; // 24-26
	/**
	 * @brief Unknown. Sometimes can be 0x2 on startup.
	 */
	unsigned short unk_0x1a; // 26-28
	/**
	 * @brief The multipress events.
	 */
	UIMultipressEvent multipress_events[8]; // 28-124
}ui_event_prime_s;


void EnqueueEvent(UIMultipressEvent uime);
