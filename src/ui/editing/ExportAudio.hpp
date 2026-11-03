#pragma once

/// Export audio (docs/design/AUDIO_MIXER.md §8.6): the master, or the master and stems, as WAV files.
namespace tessera {

class EditorContext;

namespace exportAudio {

/// The export dialog: what (the master, or with stems), the format (24-bit or 32-bit float), what it will play
/// (it follows Plays), and Export…, which asks where to save and starts.
void openDialog(EditorContext& ctx);

} // namespace exportAudio
} // namespace tessera
