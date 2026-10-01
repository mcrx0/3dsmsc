#include "test_suites.hpp"

int main() {
  run_settings_tests();
  run_library_tests();
  run_tags_tests();
  run_audio_tests();
  run_queue_tests();
  run_ui_tests();
  run_playback_tests();
  return 0;
}
