/*
 * vkQuake RmlUI - ICommandExecutor Port Interface
 *
 * Abstracts console command execution for testability.
 * Infrastructure layer provides the Quake engine implementation.
 */

#ifndef QRMLUI_PORTS_COMMAND_EXECUTOR_H
#define QRMLUI_PORTS_COMMAND_EXECUTOR_H

#include <string>

namespace QRmlUI {

// Interface for command execution
// Implemented by infrastructure layer (QuakeCommandExecutor)
class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;

    // Execute a console command (adds to command buffer)
    // Command should NOT include trailing newline - implementation adds it
    virtual void Execute(const std::string& command) = 0;

    // Execute a command immediately (inserts at front of buffer)
    virtual void ExecuteImmediate(const std::string& command) = 0;
};

} // namespace QRmlUI

#endif // QRMLUI_PORTS_COMMAND_EXECUTOR_H
