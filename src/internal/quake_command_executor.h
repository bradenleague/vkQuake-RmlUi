/*
 * vkQuake RmlUI - Quake Command Executor Implementation
 *
 * Implements ICommandExecutor using Quake's command buffer.
 */

#ifndef QRMLUI_QUAKE_COMMAND_EXECUTOR_H
#define QRMLUI_QUAKE_COMMAND_EXECUTOR_H

#include "../types/command_executor.h"

namespace QRmlUI {

// Quake engine implementation of ICommandExecutor
class QuakeCommandExecutor : public ICommandExecutor {
public:
    // Singleton access - the executor is stateless, just wraps engine functions
    static QuakeCommandExecutor& Instance();

    void Execute(const std::string& command) override;
    void ExecuteImmediate(const std::string& command) override;

private:
    QuakeCommandExecutor() = default;
};

} // namespace QRmlUI

#endif // QRMLUI_QUAKE_COMMAND_EXECUTOR_H
