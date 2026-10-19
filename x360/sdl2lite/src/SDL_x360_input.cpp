/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute), SDL2-lite from the Sonic 3 A.I.R. port.
*	SDL2-lite: events, joysticks and game controllers on top of XInput.
*
*	Each XInput controller (user index 0..3) is an SDL joystick with a fixed layout, and the game
*	controller bindings describe that layout (what Oxygen's InputManager in S3AIR queries), and the
*	SDL_GameControllerGetButton / GetAxis functions read XInput directly (what RSDKv4 uses):
*	  buttons: 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 BACK, 7 START, 8 left stick, 9 right stick
*	  axes:    0 left X, 1 left Y, 2 right X, 3 right Y (Y positive = down, as in SDL), 4 LT, 5 RT
*	  hat 0:   the D-pad
*	SDL_PollEvent reports controllers connecting / disconnecting as SDL_JOYDEVICEADDED / _REMOVED, each followed
*	by SDL_CONTROLLERDEVICEADDED / _REMOVED ("which" = device index when added, instance id when removed, as in SDL).
*	There is no keyboard (SDL_GetKeyboardState reports no key down), mouse, touch, haptics or text input.
*/

#include "SDL_x360_internal.h"

#include <string.h>


struct _SDL_Joystick
{
	int mUserIndex = 0;
};

struct _SDL_GameController
{
	SDL_Joystick* mJoystick = nullptr;
};


namespace
{
	const int MAX_CONTROLLERS = 4;
	const int NUM_BUTTONS = 10;
	const int NUM_AXES = 6;
	const int NUM_HATS = 1;

	const WORD BUTTON_MASKS[NUM_BUTTONS] =
	{
		XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
		XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
		XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
		XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB
	};

	struct ControllerState
	{
		bool mConnected = false;
		bool mReportedConnected = false;	// What the engine was told through device events
		XINPUT_GAMEPAD mGamepad;
		Uint32 mRumbleEndTicks = 0;
		bool mRumbling = false;
	};

	ControllerState gControllers[MAX_CONTROLLERS];
	SDL_Joystick gJoysticks[MAX_CONTROLLERS];
	SDL_GameController gGameControllers[MAX_CONTROLLERS];
	DWORD gLastPollTicks = 0;
	bool gEverPolled = false;
	SDL_Event gPendingControllerEvent = {};
	Uint8 gKeyboardState[SDL_NUM_SCANCODES] = {};

	void pollControllers(bool force)
	{
		const DWORD now = GetTickCount();
		if (!force && gEverPolled && now - gLastPollTicks < 2)
			return;
		gLastPollTicks = now;
		gEverPolled = true;

		for (int i = 0; i < MAX_CONTROLLERS; ++i)
		{
			ControllerState& controller = gControllers[i];
			XINPUT_STATE state;
			ZeroMemory(&state, sizeof(state));
			controller.mConnected = (XInputGetState(i, &state) == ERROR_SUCCESS);
			if (controller.mConnected)
				controller.mGamepad = state.Gamepad;
			else
				ZeroMemory(&controller.mGamepad, sizeof(controller.mGamepad));

			// Stop timed rumble
			if (controller.mRumbling && (Sint32)(SDL_GetTicks() - controller.mRumbleEndTicks) >= 0)
			{
				XINPUT_VIBRATION vibration = { 0, 0 };
				XInputSetState(i, &vibration);
				controller.mRumbling = false;
			}
		}
	}

	const ControllerState* getController(SDL_Joystick* joystick)
	{
		if (nullptr == joystick || joystick->mUserIndex < 0 || joystick->mUserIndex >= MAX_CONTROLLERS)
			return nullptr;
		pollControllers(false);
		return &gControllers[joystick->mUserIndex];
	}

	// The i-th connected controller, as SDL counts joysticks
	int getUserIndexForDeviceIndex(int deviceIndex)
	{
		pollControllers(false);
		int count = 0;
		for (int i = 0; i < MAX_CONTROLLERS; ++i)
		{
			if (gControllers[i].mConnected)
			{
				if (count == deviceIndex)
					return i;
				++count;
			}
		}
		return -1;
	}

	Sint16 convertTrigger(BYTE value)
	{
		return (Sint16)(((int)value * 32767) / 255);
	}

	Sint16 invertAxis(SHORT value)
	{
		return (Sint16)((value == -32768) ? 32767 : -value);
	}
}


extern "C"
{

/* ----- Events ----- */

void SDL_PumpEvents(void)
{
	pollControllers(true);
}

int SDL_PollEvent(SDL_Event* event)
{
	// Second half of a connection change: the game controller event that follows the joystick event
	if (gPendingControllerEvent.type != 0)
	{
		if (nullptr != event)
		{
			*event = gPendingControllerEvent;
			gPendingControllerEvent.type = 0;
		}
		return 1;
	}

	pollControllers(false);

	// Report controller connection changes, one change per call
	for (int i = 0; i < MAX_CONTROLLERS; ++i)
	{
		ControllerState& controller = gControllers[i];
		if (controller.mConnected != controller.mReportedConnected)
		{
			if (nullptr == event)
				return 1;		// Only peeking: report the change on the next real call

			// The device index of a newly added controller: the number of connected controllers before it
			int deviceIndex = 0;
			for (int j = 0; j < i; ++j)
			{
				if (gControllers[j].mConnected)
					++deviceIndex;
			}

			controller.mReportedConnected = controller.mConnected;
			memset(event, 0, sizeof(SDL_Event));
			event->type = controller.mConnected ? SDL_JOYDEVICEADDED : SDL_JOYDEVICEREMOVED;
			event->jdevice.timestamp = SDL_GetTicks();
			event->jdevice.which = controller.mConnected ? deviceIndex : i;

			memset(&gPendingControllerEvent, 0, sizeof(SDL_Event));
			gPendingControllerEvent.type = controller.mConnected ? SDL_CONTROLLERDEVICEADDED : SDL_CONTROLLERDEVICEREMOVED;
			gPendingControllerEvent.cdevice.timestamp = event->jdevice.timestamp;
			gPendingControllerEvent.cdevice.which = controller.mConnected ? deviceIndex : i;
			return 1;
		}
	}
	return 0;
}

int SDL_PushEvent(SDL_Event* event)
{
	return 0;	// Nothing in SDL2-lite listens to application events
}


/* ----- Joysticks ----- */

int SDL_NumJoysticks(void)
{
	pollControllers(false);
	int count = 0;
	for (int i = 0; i < MAX_CONTROLLERS; ++i)
	{
		if (gControllers[i].mConnected)
			++count;
	}
	return count;
}

SDL_Joystick* SDL_JoystickOpen(int device_index)
{
	const int userIndex = getUserIndexForDeviceIndex(device_index);
	if (userIndex < 0)
	{
		SDL_SetError("Joystick %d is not connected", device_index);
		return nullptr;
	}
	gJoysticks[userIndex].mUserIndex = userIndex;
	return &gJoysticks[userIndex];
}

void SDL_JoystickClose(SDL_Joystick* joystick)  {}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* joystick)
{
	return (nullptr != joystick) ? (SDL_JoystickID)joystick->mUserIndex : -1;
}

const char* SDL_JoystickName(SDL_Joystick* joystick)
{
	return "Xbox 360 Controller";
}

int SDL_JoystickNumButtons(SDL_Joystick* joystick)  { return (nullptr != joystick) ? NUM_BUTTONS : 0; }
int SDL_JoystickNumAxes(SDL_Joystick* joystick)     { return (nullptr != joystick) ? NUM_AXES : 0; }
int SDL_JoystickNumHats(SDL_Joystick* joystick)     { return (nullptr != joystick) ? NUM_HATS : 0; }

Uint8 SDL_JoystickGetButton(SDL_Joystick* joystick, int button)
{
	const ControllerState* controller = getController(joystick);
	if (nullptr == controller || button < 0 || button >= NUM_BUTTONS)
		return 0;
	return (controller->mGamepad.wButtons & BUTTON_MASKS[button]) ? 1 : 0;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick* joystick, int axis)
{
	const ControllerState* controller = getController(joystick);
	if (nullptr == controller)
		return 0;
	const XINPUT_GAMEPAD& gamepad = controller->mGamepad;
	switch (axis)
	{
		case 0:  return gamepad.sThumbLX;
		case 1:  return invertAxis(gamepad.sThumbLY);
		case 2:  return gamepad.sThumbRX;
		case 3:  return invertAxis(gamepad.sThumbRY);
		case 4:  return convertTrigger(gamepad.bLeftTrigger);
		case 5:  return convertTrigger(gamepad.bRightTrigger);
		default: return 0;
	}
}

Uint8 SDL_JoystickGetHat(SDL_Joystick* joystick, int hat)
{
	const ControllerState* controller = getController(joystick);
	if (nullptr == controller || hat != 0)
		return SDL_HAT_CENTERED;
	const WORD buttons = controller->mGamepad.wButtons;
	Uint8 value = SDL_HAT_CENTERED;
	if (buttons & XINPUT_GAMEPAD_DPAD_UP)     value |= SDL_HAT_UP;
	if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT)  value |= SDL_HAT_RIGHT;
	if (buttons & XINPUT_GAMEPAD_DPAD_DOWN)   value |= SDL_HAT_DOWN;
	if (buttons & XINPUT_GAMEPAD_DPAD_LEFT)   value |= SDL_HAT_LEFT;
	return value;
}

SDL_bool SDL_JoystickHasRumble(SDL_Joystick* joystick)
{
	return (nullptr != joystick) ? SDL_TRUE : SDL_FALSE;
}

int SDL_JoystickRumble(SDL_Joystick* joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms)
{
	if (nullptr == joystick)
		return SDL_InvalidParamError("joystick");
	const int userIndex = joystick->mUserIndex;
	XINPUT_VIBRATION vibration;
	vibration.wLeftMotorSpeed = low_frequency_rumble;
	vibration.wRightMotorSpeed = high_frequency_rumble;
	XInputSetState(userIndex, &vibration);

	ControllerState& controller = gControllers[userIndex];
	controller.mRumbling = (low_frequency_rumble != 0 || high_frequency_rumble != 0);
	controller.mRumbleEndTicks = SDL_GetTicks() + duration_ms;
	return 0;
}


/* ----- Game controllers (bindings describe the fixed joystick layout above) ----- */

SDL_GameController* SDL_GameControllerOpen(int joystick_index)
{
	SDL_Joystick* joystick = SDL_JoystickOpen(joystick_index);
	if (nullptr == joystick)
		return nullptr;
	SDL_GameController& controller = gGameControllers[joystick->mUserIndex];
	controller.mJoystick = joystick;
	return &controller;
}

const char* SDL_GameControllerName(SDL_GameController* gamecontroller)
{
	return "Xbox 360 Controller";
}

SDL_GameControllerButtonBind SDL_GameControllerGetBindForButton(SDL_GameController* gamecontroller, SDL_GameControllerButton button)
{
	SDL_GameControllerButtonBind bind;
	memset(&bind, 0, sizeof(bind));
	bind.bindType = SDL_CONTROLLER_BINDTYPE_NONE;

	int joystickButton = -1;
	int hatMask = 0;
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_A:				joystickButton = 0;  break;
		case SDL_CONTROLLER_BUTTON_B:				joystickButton = 1;  break;
		case SDL_CONTROLLER_BUTTON_X:				joystickButton = 2;  break;
		case SDL_CONTROLLER_BUTTON_Y:				joystickButton = 3;  break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:	joystickButton = 4;  break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:	joystickButton = 5;  break;
		case SDL_CONTROLLER_BUTTON_BACK:			joystickButton = 6;  break;
		case SDL_CONTROLLER_BUTTON_START:			joystickButton = 7;  break;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK:		joystickButton = 8;  break;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK:		joystickButton = 9;  break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:			hatMask = SDL_HAT_UP;     break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:		hatMask = SDL_HAT_DOWN;   break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:		hatMask = SDL_HAT_LEFT;   break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:		hatMask = SDL_HAT_RIGHT;  break;
		default:	break;	// GUIDE is not readable through XInputGetState
	}

	if (joystickButton >= 0)
	{
		bind.bindType = SDL_CONTROLLER_BINDTYPE_BUTTON;
		bind.value.button = joystickButton;
	}
	else if (hatMask != 0)
	{
		bind.bindType = SDL_CONTROLLER_BINDTYPE_HAT;
		bind.value.hat.hat = 0;
		bind.value.hat.hat_mask = hatMask;
	}
	return bind;
}

SDL_GameControllerButtonBind SDL_GameControllerGetBindForAxis(SDL_GameController* gamecontroller, SDL_GameControllerAxis axis)
{
	SDL_GameControllerButtonBind bind;
	memset(&bind, 0, sizeof(bind));
	bind.bindType = SDL_CONTROLLER_BINDTYPE_NONE;
	if (axis >= SDL_CONTROLLER_AXIS_LEFTX && axis <= SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
	{
		// Same order as the joystick axes above
		bind.bindType = SDL_CONTROLLER_BINDTYPE_AXIS;
		bind.value.axis = (int)axis;
	}
	return bind;
}

int SDL_GameControllerSetLED(SDL_GameController* gamecontroller, Uint8 red, Uint8 green, Uint8 blue)
{
	return SDL_Unsupported();
}

SDL_bool SDL_IsGameController(int joystick_index)
{
	return (getUserIndexForDeviceIndex(joystick_index) >= 0) ? SDL_TRUE : SDL_FALSE;
}

void SDL_GameControllerClose(SDL_GameController* gamecontroller)  {}

SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* gamecontroller)
{
	return (nullptr != gamecontroller) ? gamecontroller->mJoystick : nullptr;
}

SDL_GameController* SDL_GameControllerFromInstanceID(SDL_JoystickID joyid)
{
	if (joyid < 0 || joyid >= MAX_CONTROLLERS || nullptr == gGameControllers[joyid].mJoystick)
		return nullptr;
	return &gGameControllers[joyid];
}

SDL_bool SDL_GameControllerGetAttached(SDL_GameController* gamecontroller)
{
	if (nullptr == gamecontroller || nullptr == gamecontroller->mJoystick)
		return SDL_FALSE;
	const ControllerState* controller = getController(gamecontroller->mJoystick);
	return (nullptr != controller && controller->mConnected) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool SDL_JoystickGetAttached(SDL_Joystick* joystick)
{
	const ControllerState* controller = getController(joystick);
	return (nullptr != controller && controller->mConnected) ? SDL_TRUE : SDL_FALSE;
}

Uint8 SDL_GameControllerGetButton(SDL_GameController* gamecontroller, SDL_GameControllerButton button)
{
	if (nullptr == gamecontroller)
		return 0;
	const ControllerState* controller = getController(gamecontroller->mJoystick);
	if (nullptr == controller)
		return 0;
	WORD mask = 0;
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_A:				mask = XINPUT_GAMEPAD_A;  break;
		case SDL_CONTROLLER_BUTTON_B:				mask = XINPUT_GAMEPAD_B;  break;
		case SDL_CONTROLLER_BUTTON_X:				mask = XINPUT_GAMEPAD_X;  break;
		case SDL_CONTROLLER_BUTTON_Y:				mask = XINPUT_GAMEPAD_Y;  break;
		case SDL_CONTROLLER_BUTTON_BACK:			mask = XINPUT_GAMEPAD_BACK;  break;
		case SDL_CONTROLLER_BUTTON_START:			mask = XINPUT_GAMEPAD_START;  break;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK:		mask = XINPUT_GAMEPAD_LEFT_THUMB;  break;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK:		mask = XINPUT_GAMEPAD_RIGHT_THUMB;  break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:	mask = XINPUT_GAMEPAD_LEFT_SHOULDER;  break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:	mask = XINPUT_GAMEPAD_RIGHT_SHOULDER;  break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:			mask = XINPUT_GAMEPAD_DPAD_UP;  break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:		mask = XINPUT_GAMEPAD_DPAD_DOWN;  break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:		mask = XINPUT_GAMEPAD_DPAD_LEFT;  break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:		mask = XINPUT_GAMEPAD_DPAD_RIGHT;  break;
		default:	return 0;	// GUIDE is not readable through XInputGetState; the rest doesn't exist on the pad
	}
	return (controller->mGamepad.wButtons & mask) ? 1 : 0;
}

Sint16 SDL_GameControllerGetAxis(SDL_GameController* gamecontroller, SDL_GameControllerAxis axis)
{
	if (nullptr == gamecontroller || axis < SDL_CONTROLLER_AXIS_LEFTX || axis > SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
		return 0;
	// Same order as the joystick axes
	return SDL_JoystickGetAxis(gamecontroller->mJoystick, (int)axis);
}

int SDL_GameControllerEventState(int state)
{
	return SDL_ENABLE;
}

int SDL_GameControllerAddMapping(const char* mappingString)
{
	return 0;	// The layout is fixed
}

int SDL_GameControllerAddMappingsFromRW(SDL_RWops* rw, int freerw)
{
	if (freerw && nullptr != rw)
		SDL_RWclose(rw);
	return 0;
}


/* ----- Haptics: not available (rumble goes through SDL_JoystickRumble) ----- */

SDL_Haptic* SDL_HapticOpenFromJoystick(SDL_Joystick* joystick)
{
	SDL_SetError("Haptic devices are not supported on Xbox 360 (SDL2-lite)");
	return nullptr;
}

void SDL_HapticClose(SDL_Haptic* haptic)  {}

int SDL_HapticRumbleInit(SDL_Haptic* haptic)
{
	return SDL_Unsupported();
}

int SDL_HapticRumblePlay(SDL_Haptic* haptic, float strength, Uint32 length)
{
	return SDL_Unsupported();
}

int SDL_HapticRumbleStop(SDL_Haptic* haptic)
{
	return SDL_Unsupported();
}


/* ----- Keyboard / text input: none on the console ----- */

const Uint8* SDL_GetKeyboardState(int* numkeys)
{
	if (nullptr != numkeys)
		*numkeys = SDL_NUM_SCANCODES;
	return gKeyboardState;
}

SDL_Keymod SDL_GetModState(void)
{
	return KMOD_NONE;
}

void SDL_StartTextInput(void)  {}
void SDL_StopTextInput(void)  {}

SDL_bool SDL_IsTextInputActive(void)
{
	return SDL_FALSE;
}


/* ----- Touch: none ----- */

int SDL_GetNumTouchDevices(void)
{
	return 0;
}

SDL_TouchID SDL_GetTouchDevice(int index)
{
	SDL_SetError("No touch devices");
	return 0;
}

int SDL_GetNumTouchFingers(SDL_TouchID touchID)
{
	return 0;
}

SDL_Finger* SDL_GetTouchFinger(SDL_TouchID touchID, int index)
{
	return nullptr;
}

}
