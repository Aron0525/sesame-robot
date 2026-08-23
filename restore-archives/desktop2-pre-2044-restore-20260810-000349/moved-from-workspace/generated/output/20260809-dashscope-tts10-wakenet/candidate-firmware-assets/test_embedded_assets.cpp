#include <cstring>
#include "zhima_wakeword_config.h"
#include "zhima_wakeword_model_data.h"

int main() {
  using namespace sesame::voice::zhima;
  if (kModelDataLen != static_cast<int>(kModelBytes)) return 1;
  if (kSampleRateHz != 16000 || kFrameCount != 33 || kFeatureBins != 32) return 2;
  if (kInputScale <= 0.0f || kFeatureStd <= 0.0f) return 3;
  if (std::strcmp(kModelName, "zhima_wakeword_dashscope10_int8") != 0) return 4;
  return 0;
}
