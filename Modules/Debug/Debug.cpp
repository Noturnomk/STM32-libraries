/* Copyright (C) 2018-2019 Thomas Jespersen, TKJ Electronics. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the MIT License
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the MIT License for further details.
 *
 * Contact information
 * ------------------------------------------
 * Thomas Jespersen, TKJ Electronics
 * Web      :  http://www.tkjelectronics.dk
 * e-mail   :  thomasj@tkjelectronics.dk
 * ------------------------------------------
 */
 
#include "Debug.h"
#include "cmsis_os.h"
#ifdef DEBUG_PRINTF_ENABLED
#include "LSPC.hpp"
#endif
#include "IO.h"

bool Debug::handleCreated = false;
Debug Debug::debugHandle;

// Necessary to export for compiler such that the Error_Handler function can be called by C code
extern "C" __EXPORT void Error_Handler(void);
extern "C" __EXPORT void Debug_print(const char * msg);
extern "C" __EXPORT void Debug_Pulse();

Debug::Debug() : com_(0), debugPulsePin_(0)
{
	if (handleCreated) {
		ERROR("Debug object already created");
		return;
	}

	handleCreated = true;
}

Debug::~Debug()
{
}

void Debug::AssignDebugCOM(void * com)
{
	debugHandle.com_ = com;

#ifdef DEBUG_PRINTF_ENABLED
	if (!com) {
		ERROR("LSPC object does not exist");
		return;
	}
#endif

#ifdef DEBUG_PRINTF_ENABLED
	debugHandle.currentBufferLocation_ = 0;
	memset(debugHandle.messageBuffer_, 0, MAX_DEBUG_TEXT_LENGTH);
#ifdef USE_FREERTOS
	debugHandle.mutex_ = xSemaphoreCreateBinary();
	if (debugHandle.mutex_ == NULL) {
		ERROR("Could not create Debug mutex");
		return;
	}
	vQueueAddToRegistry(debugHandle.mutex_, "Debug mutex");
	xSemaphoreGive( debugHandle.mutex_ ); // give the semaphore the first time

	xTaskCreate( Debug::PackageGeneratorThread, (char *)"Debug transmitter", debugHandle.THREAD_STACK_SIZE, (void*) &debugHandle, debugHandle.THREAD_PRIORITY, &debugHandle._TaskHandle);
#endif
#endif
}

#ifdef DEBUG_PRINTF_ENABLED
#ifdef USE_FREERTOS
void Debug::PackageGeneratorThread(void * pvParameters)
{
	Debug * debug = (Debug *)pvParameters;

	while (1)
	{
		osDelay(1);
		xSemaphoreTake( debug->mutex_, ( TickType_t ) portMAX_DELAY ); // take debug mutex
		if (debug->currentBufferLocation_ > 0) {
			((LSPC*)debug->com_)->TransmitAsync(lspc::MessageTypesToPC::Debug, (const uint8_t *)debug->messageBuffer_, debug->currentBufferLocation_);
			debug->currentBufferLocation_ = 0;
		}
		xSemaphoreGive( debug->mutex_ ); // give hardware resource back
	}
}
#endif
#endif

void Debug::Message(const char * msg)
{
#ifdef DEBUG_PRINTF_ENABLED
	if (!debugHandle.com_) return;
	if (!((LSPC*)debugHandle.com_)->Connected()) return;

	#ifdef USE_FREERTOS
	xSemaphoreTake( debugHandle.mutex_, ( TickType_t ) portMAX_DELAY ); // take debug mutex
	#endif

	uint16_t stringLength = strlen(msg);
	if (stringLength > MAX_DEBUG_TEXT_LENGTH) { // message is too long to fit in one package
		// Send current buffered package now and clear buffer
		((LSPC*)debugHandle.com_)->TransmitAsync(lspc::MessageTypesToPC::Debug, (const uint8_t *)debugHandle.messageBuffer_, debugHandle.currentBufferLocation_);
		debugHandle.currentBufferLocation_ = 0;

		uint8_t * msgPtr = (uint8_t *)msg;
		while (stringLength > 0) { // split the message up in seperate packages
			uint16_t sendLength = stringLength;
			if (sendLength > MAX_DEBUG_TEXT_LENGTH) sendLength = MAX_DEBUG_TEXT_LENGTH;
			((LSPC*)debugHandle.com_)->TransmitAsync(lspc::MessageTypesToPC::Debug, (const uint8_t *)msgPtr, sendLength);
			msgPtr += sendLength;
			stringLength -= sendLength;
		}
	} else { // package can fit in one package
		if (stringLength > (MAX_DEBUG_TEXT_LENGTH-debugHandle.currentBufferLocation_)) {// stringLength = (MAX_DEBUG_TEXT_LENGTH-debugHandle.currentBufferLocation_); // "cut away" any parts above the maximum string length
			// Send package now and clear buffer
			((LSPC*)debugHandle.com_)->TransmitAsync(lspc::MessageTypesToPC::Debug, (const uint8_t *)debugHandle.messageBuffer_, debugHandle.currentBufferLocation_);
			debugHandle.currentBufferLocation_ = 0;
		}

		memcpy(&debugHandle.messageBuffer_[debugHandle.currentBufferLocation_], msg, stringLength);
		debugHandle.currentBufferLocation_ += stringLength;
	}
	#if USE_FREERTOS
	xSemaphoreGive( debugHandle.mutex_ ); // give hardware resource back
	#endif
#endif
}

void Debug::Message(std::string msg)
{
	Message(msg.c_str());
	Message("\n");
}

void Debug::Message(const char * functionName, const char * msg)
{
	Message("[");
	Message(functionName);
	Message("] ");
	Message(msg);
	Message("\n");
}

void Debug::Message(const char * functionName, std::string msg)
{
	Message("[");
	Message(functionName);
	Message("] ");
	Message(msg.c_str());
	Message("\n");
}

void Debug::Message(const char * type, const char * functionName, const char * msg)
{
	Message(type);
	Message("[");
	Message(functionName);
	Message("] ");
	Message(msg);
	Message("\n");
}

void Debug::Message(std::string type, const char * functionName, std::string msg)
{
	Message("[");
	Message(functionName);
	Message("] ");
	Message(msg.c_str());
	Message("\n");
}

void Debug::print(const char * msg)
{
	Message(msg);
}

void Debug::printf( const char *msgFmt, ... )
{
#ifdef DEBUG_PRINTF_ENABLED
	va_list args;

	if (!debugHandle.com_) return;
	if (!((LSPC*)debugHandle.com_)->Connected()) return;

	va_start( args,  msgFmt );

	char * strBuf = (char *) pvPortMalloc(MAX_DEBUG_TEXT_LENGTH);
	if (!strBuf) return;

	vsnprintf( strBuf, MAX_DEBUG_TEXT_LENGTH, msgFmt, args );

	Message(strBuf);

	vPortFree(strBuf);

	va_end( args );
#endif
}

void Debug::Error(const char * type, const char * functionName, const char * msg)
{
	// At errors do not continue current task/thread but print instead the error message repeatedly
	__asm__("BKPT");
	while (1)
	{
		Debug::Message(type, functionName, msg);
		osDelay(500);
	}
}

void Debug::SetDebugPin(void * pin)
{
	if (debugHandle.debugPulsePin_) return; // pin already set
	debugHandle.debugPulsePin_ = pin;
}

void Debug::Pulse()
{
	if (!debugHandle.debugPulsePin_) return;
	((IO*)debugHandle.debugPulsePin_)->High();
	osDelay(50);
	((IO*)debugHandle.debugPulsePin_)->Low();
}

void Debug::Toggle()
{
	if (!debugHandle.debugPulsePin_) return;
	((IO*)debugHandle.debugPulsePin_)->Toggle();
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
	Debug::Error("ERROR: ", "Error_Handler", "Global ");
}

void Debug_print(const char * msg)
{
	Debug::print(msg);
}

void Debug_Pulse()
{
	Debug::Pulse();
}
