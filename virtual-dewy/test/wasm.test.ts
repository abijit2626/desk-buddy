import { describe, expect, it } from "vitest";
import { bootCore } from "./helpers";

describe("the real firmware, compiled to WebAssembly", () => {
  it("boots and prints its own start-up lines", async () => {
    const d = await bootCore();
    const out = d.run(500);
    console.log(out);
    console.log(d.core.status());
    console.log(d.core.pins());
    console.log(d.core.config());
    expect(d.core.moodNames()).toContain("listening");
  });
});
