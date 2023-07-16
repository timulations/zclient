# Read the process ID from the file
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/mock_server_pid.txt" PID_FILE_CONTENT)
string(STRIP "${PID_FILE_CONTENT}" PID)

# Kill the process using the captured PID
if (WIN32)
  execute_process(
    COMMAND taskkill /F /PID ${PID}
  )
else ()
  execute_process(
    COMMAND kill ${PID}
  )
endif ()

MESSAGE("Killed NodeJS server at ${PID}")