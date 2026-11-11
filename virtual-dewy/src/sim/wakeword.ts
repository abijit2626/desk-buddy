// "Hey Dewy" is NOT simulated. The model in voice/wakeword/model is a microWakeWord network that would run on the
// ESP32 itself; nothing in this page runs it. These are only the model's published settings (hey_dewy.json), shown by
// the `ww debug` command so you can see what the real thing will be tuned to. test/voice.test.ts checks them against
// the JSON file, so they cannot drift.
export const WAKE_WORD = {
  phrase: "Hey Dewy",
  probabilityCutoff: 0.98,
  slidingWindow: 5,
  featureStepMs: 10,
  tensorArenaBytes: 30000,
  modelBytes: 62304,
} as const;
export const WAKE_WORD_LABEL = "NOT SIMULATED: the wake word model is not run in this page. The 'Hey Dewy' button only starts a question.";
