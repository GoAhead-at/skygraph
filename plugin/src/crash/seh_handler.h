#pragma once

namespace skygraph::transport { class WriterThread; class RollingRecorder; }

namespace skygraph::crash {

// Installs a Vectored Exception Handler that catches "fatal" exceptions
// (access violations, illegal instruction, stack overflow, etc.), emits a
// best-effort `event.crash` record to both the pipe and the recorder, flushes
// the recorder, and then returns EXCEPTION_CONTINUE_SEARCH so any other
// installed handler (NetScriptFramework, third-party crash loggers) and
// finally the OS default get a chance to do their thing.
//
// Call at the very end of SKSEPluginLoad so we run *first* in the VEH chain
// (Windows fires VEHs in registration order); subsequent loaders' VEHs still
// fire after ours. Safe to call once.
//
// Wires recorder pointer once so the handler doesn't need to chase a global.
void Install(transport::WriterThread* a_writer,
             transport::RollingRecorder* a_recorder) noexcept;

}  // namespace skygraph::crash
