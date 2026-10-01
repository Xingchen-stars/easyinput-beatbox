import { describe, expect, it } from "vitest";

import { syncEditBankFromVariation } from "./bank-sync";

describe("device variation bank sync", () => {
  it("moves the editor from A to B when the device changes variation", () => {
    expect(syncEditBankFromVariation(0, 0, 1)).toBe(1);
  });

  it("moves the editor from B to A when the device changes variation", () => {
    expect(syncEditBankFromVariation(1, 1, 0)).toBe(0);
  });

  it("keeps Fill selected for unrelated state packets", () => {
    expect(syncEditBankFromVariation(2, 0, 0)).toBe(2);
  });
});
