#pragma once

#include "transport/writer_thread.h"

namespace skygraph::samplers::event_sources {

// Installs the RE-level event sinks (cell attach/detach, mod events) and
// emits the one-shot `plugins` record. Safe to call once at kDataLoaded.
//
// Save / autosave events flow through the SKSE messaging interface (more
// stable than RE-level save events across CommonLibSSE-NG versions); the
// plugin entry routes those to EmitSaveEvent / EmitAutosaveEvent below.
void InstallAll(transport::WriterThread& a_writer);

void EmitSaveEvent(const char* a_name);
void EmitAutosaveEvent();

}  // namespace skygraph::samplers::event_sources
