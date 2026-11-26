/**
 * @file forwarding.cpp
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief Event source forwarding
 * @details
 * @version 0.1
 * @date 2020-11-16
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

//
// Global Variables
//
extern UINT64     g_OutputSourceTag;
extern LIST_ENTRY g_OutputSources;
extern LIST_ENTRY g_EventTrace;

/**
 * @brief Get the output source tag and increase the
 * global variable for tag
 *
 * @return UINT64
 */
UINT64
ForwardingGetNewOutputSourceTag()
{
    return g_OutputSourceTag++;
}

/**
 * @brief Opens the output source
 * @param SourceDescriptor Descriptor of the source
 *
 * @return DEBUGGER_OUTPUT_SOURCE_STATUS return status of the opening function
 */
DEBUGGER_OUTPUT_SOURCE_STATUS
ForwardingOpenOutputSource(PDEBUGGER_EVENT_FORWARDING SourceDescriptor)
{
    //
    // Check if already closed
    //
    if (SourceDescriptor->State == EVENT_FORWARDING_CLOSED)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_ALREADY_CLOSED;
    }

    //
    // check if already opened
    //
    if (SourceDescriptor->State == EVENT_FORWARDING_STATE_OPENED)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_ALREADY_OPENED;
    }

    //
    // Set the status to opened
    //
    SourceDescriptor->State = EVENT_FORWARDING_STATE_OPENED;

    //
    // Now, it's time to open the source based on its type
    //
    if (SourceDescriptor->Type == EVENT_FORWARDING_FILE)
    {
        //
        // Nothing special to do here, file is opened with CreateFile
        // and nothing should be called to open the handle
        //
        return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_OPENED;
    }
    else if (SourceDescriptor->Type == EVENT_FORWARDING_NAMEDPIPE)
    {
        //
        // Nothing special to do here, namedpipe is opened with CreateFile
        // and nothing should be called to open the handle
        //
        return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_OPENED;
    }
    else if (SourceDescriptor->Type == EVENT_FORWARDING_TCP)
    {
        //
        // Nothing special to do here, tcp socket is opened with
        // CommunicationClientConnectToServer and nothing should be
        // called to open the socket
        //
        return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_OPENED;
    }
    else if (SourceDescriptor->Type == EVENT_FORWARDING_MODULE)
    {
        //
        // Nothing special to do here, function is found previously
        // and nothing should be called to open the module
        //
        return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_OPENED;
    }

    return DEBUGGER_OUTPUT_SOURCE_STATUS_UNKNOWN_ERROR;
}

/**
 * @brief Releases the resources (handle, socket, module) of the output source
 * @param SourceDescriptor Descriptor of the source
 *
 * @details The caller is responsible for checking and setting the state
 *
 * @return BOOLEAN whether the type of the source was valid or not
 */
static BOOLEAN
ForwardingReleaseOutputSource(PDEBUGGER_EVENT_FORWARDING SourceDescriptor)
{
    switch (SourceDescriptor->Type)
    {
    case EVENT_FORWARDING_FILE:

        //
        // Close the handle
        //
        PlatformCloseFile(SourceDescriptor->Handle);
        return TRUE;

    case EVENT_FORWARDING_TCP:

        //
        // Shutdown connection and cleanup
        //
        CommunicationClientShutdownConnection(SourceDescriptor->Socket);
        CommunicationClientCleanup(SourceDescriptor->Socket);
        return TRUE;

    case EVENT_FORWARDING_NAMEDPIPE:

        //
        // Close the pipe
        //
        NamedPipeClientClosePipe(SourceDescriptor->Handle);
        return TRUE;

    case EVENT_FORWARDING_MODULE:

        //
        // Free the library
        //
        PlatformFreeLibrary(SourceDescriptor->Module);
        return TRUE;

    default:
        return FALSE;
    }
}

/**
 * @brief Closes the output source
 * @param SourceDescriptor Descriptor of the source
 *
 * @return DEBUGGER_OUTPUT_SOURCE_STATUS return status of the closing function
 */
DEBUGGER_OUTPUT_SOURCE_STATUS
ForwardingCloseOutputSource(PDEBUGGER_EVENT_FORWARDING SourceDescriptor)
{
    //
    // Check if already closed
    //
    if (SourceDescriptor->State == EVENT_FORWARDING_CLOSED)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_ALREADY_CLOSED;
    }

    //
    // Check if not opened
    //
    if (SourceDescriptor->State != EVENT_FORWARDING_STATE_OPENED)
    {
        //
        // Not opennd ? or state other than opened ?
        //
        return DEBUGGER_OUTPUT_SOURCE_STATUS_UNKNOWN_ERROR;
    }

    //
    // Set the state
    //
    SourceDescriptor->State = EVENT_FORWARDING_CLOSED;

    //
    // Now, it's time to close the source based on its type
    //
    if (ForwardingReleaseOutputSource(SourceDescriptor))
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_CLOSED;
    }

    return DEBUGGER_OUTPUT_SOURCE_STATUS_UNKNOWN_ERROR;
}

/**
 * @brief Flushes the buffered data of the output source to its target
 * @param SourceDescriptor Descriptor of the source
 *
 * @details Only file sources are buffered (by the operating system), the other
 * types send the data directly
 *
 * @return DEBUGGER_OUTPUT_SOURCE_STATUS return status of the flushing function
 */
DEBUGGER_OUTPUT_SOURCE_STATUS
ForwardingFlushOutputSource(PDEBUGGER_EVENT_FORWARDING SourceDescriptor)
{
    if (SourceDescriptor->State == EVENT_FORWARDING_CLOSED)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_ALREADY_CLOSED;
    }

    if (SourceDescriptor->State != EVENT_FORWARDING_STATE_OPENED)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_NOT_OPENED;
    }

    if (SourceDescriptor->Type != EVENT_FORWARDING_FILE)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_NOT_SUPPORTED;
    }

    if (!PlatformFlushFile(SourceDescriptor->Handle))
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_UNKNOWN_ERROR;
    }

    return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_FLUSHED;
}

/**
 * @brief Reopens the output source
 * @param SourceDescriptor Descriptor of the source
 *
 * @details The current resources of the source (if any) are released, then the
 * source is created again from its address and opened. This also makes it
 * possible to reuse a closed source. File sources are reopened in the append
 * mode, so the previously written data is kept. The output tag stays the same,
 * so the events that use this source keep forwarding to it
 *
 * @return DEBUGGER_OUTPUT_SOURCE_STATUS return status of the reopening function
 */
DEBUGGER_OUTPUT_SOURCE_STATUS
ForwardingReopenOutputSource(PDEBUGGER_EVENT_FORWARDING SourceDescriptor)
{
    PVOID   NewHandle;
    SOCKET  NewSocket = 0;
    HMODULE NewModule = NULL;

    if (SourceDescriptor->Address == NULL)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_UNKNOWN_ERROR;
    }

    //
    // Release the current resources (a closed source is already released). The
    // state is changed first, so no event is forwarded to the released handle
    //
    if (SourceDescriptor->State != EVENT_FORWARDING_CLOSED)
    {
        SourceDescriptor->State = EVENT_FORWARDING_CLOSED;
        ForwardingReleaseOutputSource(SourceDescriptor);
    }

    //
    // Create the source again, if it fails, the source remains closed
    //
    NewHandle = ForwardingCreateOutputSource(SourceDescriptor->Type,
                                             SourceDescriptor->Address,
                                             &NewSocket,
                                             &NewModule,
                                             TRUE);

    if (NewHandle == INVALID_HANDLE_VALUE)
    {
        return DEBUGGER_OUTPUT_SOURCE_STATUS_UNKNOWN_ERROR;
    }

    if (SourceDescriptor->Type == EVENT_FORWARDING_TCP)
    {
        SourceDescriptor->Socket = NewSocket;
    }
    else if (SourceDescriptor->Type == EVENT_FORWARDING_MODULE)
    {
        SourceDescriptor->Module = NewModule;
        SourceDescriptor->Handle = NewHandle;
    }
    else
    {
        SourceDescriptor->Handle = NewHandle;
    }

    SourceDescriptor->State = EVENT_FORWARDING_STATE_OPENED;

    return DEBUGGER_OUTPUT_SOURCE_STATUS_SUCCESSFULLY_REOPENED;
}

/**
 * @brief Create a new source (create handle from the source)
 * @param SourceType Type of the source
 * @param Description Description of the source
 * @param Socket Socket object in the case of TCP connection
 * @param Module Module object in the case of loading modules
 * @param AppendToFile Whether to append to the end of the file (in the case
 * of files) instead of writing from its beginning
 *
 * @details If the target connection is a tcp connection then there
 * is no handle and instead there is a socket, this way we pass a
 * valid value for handle (TRUE) which is not a valid handle but it
 * indicates that the operation was successful and the caller can use
 * the pointer that it passed as the socket.
 * On anything other than tcp sockets, the socket pointer in not
 * modified; thus, it's not value
 *
 * @return HANDLE returns handle of the source
 */
PVOID
ForwardingCreateOutputSource(DEBUGGER_EVENT_FORWARDING_TYPE SourceType,
                             const string &                 Description,
                             SOCKET *                       Socket,
                             HMODULE *                      Module,
                             BOOLEAN                        AppendToFile)
{
    string IpPortDelimiter;
    string Ip;
    string Port;

    if (SourceType == EVENT_FORWARDING_FILE)
    {
        //
        // Create a new file
        //
        HANDLE FileHandle = AppendToFile ? PlatformOpenFileForAppendingNarrow(Description.c_str())
                                         : PlatformOpenFileForWritingNarrow(Description.c_str());

        //
        // The handle might be INVALID_HANDLE_VALUE which will be
        // checked by the caller
        //
        return (PVOID)FileHandle;
    }
    else if (SourceType == EVENT_FORWARDING_MODULE)
    {
        HMODULE ModuleHandle = PlatformLoadLibrary(Description.c_str());

        if (ModuleHandle == NULL)
        {
            ShowMessages("err, unable to load the module\n");
            return INVALID_HANDLE_VALUE;
        }

        hyperdbg_event_forwarding_t hyperdbg_event_forwarding = (hyperdbg_event_forwarding_t)PlatformGetProcAddress(ModuleHandle, "hyperdbg_event_forwarding");

        if (hyperdbg_event_forwarding == NULL)
        {
            ShowMessages("err, unable to find the 'hyperdbg_event_forwarding' function\n");
            PlatformFreeLibrary(ModuleHandle);
            return INVALID_HANDLE_VALUE;
        }

        //
        // Set the module handle
        //
        *Module = ModuleHandle;

        //
        // The handle is the location of the hyperdbg_event_forwarding function
        //
        return (PVOID)hyperdbg_event_forwarding;
    }
    else if (SourceType == EVENT_FORWARDING_NAMEDPIPE)
    {
        HANDLE PipeHandle = NamedPipeClientCreatePipe(Description.c_str());

        if (!PipeHandle)
        {
            //
            // Unable to create handle
            //
            return INVALID_HANDLE_VALUE;
        }

        return (PVOID)PipeHandle;
    }
    else if (SourceType == EVENT_FORWARDING_TCP)
    {
        //
        // Check if the port is included in the description string or not
        //
        if (Description.find(':') != std::string::npos)
        {
            //
            // Split the ip and port by : delimiter
            //
            IpPortDelimiter = ':';
            SIZE_T find     = Description.find(IpPortDelimiter);
            Ip              = Description.substr(0, find);
            Port            = Description.substr(find + 1, find + Description.size());

            //
            // Connect to server, in this function 0 means there was no error
            // and 1 means there was an error
            //
            if (CommunicationClientConnectToServer(Ip.c_str(), Port.c_str(), Socket) == 0)
            {
                //
                // Send a fake handle just to avoid sending INVALID_HANDLE_VALUE
                // because this functionality doesn't work with handlers; however,
                // send 1 or TRUE is a valid handle
                //
                return (PVOID)TRUE;
            }
            else
            {
                //
                // There was an error in connecting to the server
                // so return an invalid handle
                //
                return INVALID_HANDLE_VALUE;
            }
        }
        else
        {
            //
            // Invalid address format
            //
            return INVALID_HANDLE_VALUE;
        }
    }

    //
    // By default, handle is invalid
    //
    return INVALID_HANDLE_VALUE;
}

/**
 * @brief Send the event result to the corresponding sources
 * @param EventDetail Description saved about the event in the
 * user-mode
 * @param MessageLength Length of the message
 * @details This function will not check whether the event has an
 * output source or not, the caller if this function should make
 * sure that the following event has valid output sources or not
 *
 * @return BOOLEAN whether sending results was successful or not
 */
BOOLEAN
ForwardingPerformEventForwarding(PDEBUGGER_GENERAL_EVENT_DETAIL EventDetail,
                                 CHAR *                         Message,
                                 UINT32                         MessageLength)
{
    BOOLEAN     Result   = FALSE;
    PLIST_ENTRY TempList = 0;

    for (SIZE_T i = 0; i < DebuggerOutputSourceMaximumRemoteSourceForSingleEvent; i++)
    {
        //
        // Check whether we reached to the end of the events
        //
        if (EventDetail->OutputSourceTags[i] == NULL)
        {
            return Result;
        }

        //
        // If we reach here then the output tag is not null
        // means that we should find the event tag from list
        // of tags
        //
        TempList = &g_OutputSources;

        while (&g_OutputSources != TempList->Flink)
        {
            TempList = TempList->Flink;

            PDEBUGGER_EVENT_FORWARDING CurrentOutputSourceDetails = CONTAINING_RECORD(
                TempList,
                DEBUGGER_EVENT_FORWARDING,
                OutputSourcesList);

            if (EventDetail->OutputSourceTags[i] ==
                CurrentOutputSourceDetails->OutputUniqueTag)
            {
                //
                // Indicate that we found a tag that matches this item
                // Now, we should check whether the output is opened or
                // not closed
                //
                if (CurrentOutputSourceDetails->State ==
                    EVENT_FORWARDING_STATE_OPENED)
                {
                    switch (CurrentOutputSourceDetails->Type)
                    {
                    case EVENT_FORWARDING_NAMEDPIPE:
                        Result = ForwardingSendToNamedPipe(
                            CurrentOutputSourceDetails->Handle,
                            Message,
                            MessageLength);
                        break;
                    case EVENT_FORWARDING_FILE:
                        Result = ForwardingWriteToFile(CurrentOutputSourceDetails->Handle,
                                                       Message,
                                                       MessageLength);
                        break;
                    case EVENT_FORWARDING_TCP:
                        Result = ForwardingSendToTcpSocket(
                            CurrentOutputSourceDetails->Socket,
                            Message,
                            MessageLength);
                        break;
                    case EVENT_FORWARDING_MODULE:
                        ((hyperdbg_event_forwarding_t)CurrentOutputSourceDetails->Handle)(
                            Message,
                            MessageLength);
                        Result = TRUE;
                        break;
                    default:
                        break;
                    }
                }

                //
                // No need to search through the list anymore
                //
                break;
            }
        }
    }

    return FALSE;
}

/**
 * @brief Check and send the event result to the corresponding sources
 * @param OperationCode The target operation code or tag
 * @param MessageLength Length of the message
 * @details This function will not check whether the event has an
 * output source or not, the caller if this function should make
 * sure that the following event has valid output sources or not
 *
 * @return BOOLEAN whether sending results was successful or not
 */
BOOLEAN
ForwardingCheckAndPerformEventForwarding(UINT32 OperationCode,
                                         CHAR * Message,
                                         UINT32 MessageLength)
{
    PLIST_ENTRY TempList;
    BOOLEAN     OutputSourceFound = FALSE;

    //
    // We should check whether the following flag matches
    // with an output or not, also this is not where we want to
    // check output resources
    //
    TempList = &g_EventTrace;
    while (&g_EventTrace != TempList->Blink)
    {
        TempList = TempList->Blink;

        PDEBUGGER_GENERAL_EVENT_DETAIL EventDetail = CONTAINING_RECORD(
            TempList,
            DEBUGGER_GENERAL_EVENT_DETAIL,
            CommandsEventList);

        if (EventDetail->HasCustomOutput && (UINT32)EventDetail->Tag == OperationCode)
        {
            //
            // Output source found
            //
            OutputSourceFound = TRUE;

            //
            // Send the event to output sources
            // Minus one (-1) is because we want to
            // remove the null character at end of the message
            //
            if (!ForwardingPerformEventForwarding(
                    EventDetail,
                    Message,
                    MessageLength))
            {
                ShowMessages("err, there was an error transferring the "
                             "message to the remote sources\n");
            }

            break;
        }
    }

    return OutputSourceFound;
}

/**
 * @brief Write the output results to the file
 * @param FileHandle Handle of the target file
 * @param Message The message that should be written to file
 * @param MessageLength Length of the message
 * @details This function will not check whether the event has an
 * output source or not, the caller if this function should make
 * sure that the following event has valid output sources or not
 *
 * @return BOOLEAN whether the writing to the file was successful or not
 */
BOOLEAN
ForwardingWriteToFile(HANDLE FileHandle, CHAR * Message, UINT32 MessageLength)
{
    DWORD BytesWritten = 0;
    BOOL  ErrorFlag    = FALSE;

    ErrorFlag = PlatformWriteFile(FileHandle,     // open file handle
                                  Message,        // start of data to write
                                  MessageLength); // number of bytes to write

    return TRUE;

    if (ErrorFlag == FALSE)
    {
        //
        // Err, terminal failure: Unable to write to file
        //

        return FALSE;
    }
    else
    {
        if (BytesWritten != MessageLength)
        {
            //
            // This is an error because a synchronous write that results in
            // success (WriteFile returns TRUE) should write all data as
            // requested. This would not necessarily be the case for
            // asynchronous writes.
            //

            return FALSE;
        }
        else
        {
            //
            // Successfully wrote
            //
            return TRUE;
        }
    }

    //
    // by default we assume there was an error
    //
    return FALSE;
}

/**
 * @brief Send the output results to the namedpipe
 * @param NamedPipeHandle Handle of the target namedpipe
 * @param Message The message that should be sent to namedpipe
 * @param MessageLength Length of the message
 * @details This function will not check whether the event has an
 * output source or not, the caller if this function should make
 * sure that the following event has valid output sources or not
 *
 * @return BOOLEAN whether the sending to the namedpipe was successful or not
 */
BOOLEAN
ForwardingSendToNamedPipe(HANDLE NamedPipeHandle, CHAR * Message, UINT32 MessageLength)
{
    BOOLEAN SentMessageResult;

    SentMessageResult =
        NamedPipeClientSendMessage(NamedPipeHandle, Message, MessageLength);

    if (!SentMessageResult)
    {
        //
        // Sending error
        //
        return FALSE;
    }

    //
    // Successfully sent
    //
    return TRUE;
}

/**
 * @brief Send the output results to the tcp socket
 * @param TcpSocket Socket object of the target tcp socket
 * @param Message The message that should be sent to the tcp socket
 * @param MessageLength Length of the message
 * @details This function will not check whether the event has an
 * output source or not, the caller if this function should make
 * sure that the following event has valid output sources or not
 *
 * @return BOOLEAN whether the sending to the tcp socket was successful or not
 */
BOOLEAN
ForwardingSendToTcpSocket(SOCKET TcpSocket, CHAR * Message, UINT32 MessageLength)
{
    if (CommunicationClientSendMessage(TcpSocket, Message, MessageLength) != 0)
    {
        //
        // Failed to send
        //
        return FALSE;
    }

    //
    // Successfully sent
    //
    return TRUE;
}
