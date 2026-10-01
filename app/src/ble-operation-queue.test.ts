import { describe, expect, it } from "vitest";
import { BleOperationQueue } from "./ble-operation-queue";

describe("BLE operation queue", () => {
  it("does not start the next operation until the previous promise settles", async () => {
    const queue = new BleOperationQueue();
    const calls: string[] = [];
    let release!: () => void;
    const pending = new Promise<void>((resolve) => { release = resolve; });
    const first = queue.run("first", async () => { calls.push("first"); await pending; });
    const second = queue.run("second", async () => { calls.push("second"); });
    await Promise.resolve();
    expect(calls).toEqual(["first"]);
    release();
    await Promise.all([first, second]);
    expect(calls).toEqual(["first", "second"]);
  });

  it("cancels pending work and discards an in-flight result on disconnect", async () => {
    const queue = new BleOperationQueue();
    let release!: () => void;
    const first = queue.run("first", () => new Promise<void>((resolve) => { release = resolve; }));
    let secondStarted = false;
    const second = queue.run("second", async () => { secondStarted = true; });
    const results = Promise.allSettled([first, second]);
    await Promise.resolve();
    queue.invalidate();
    release();
    expect((await results).map((result) => result.status)).toEqual(["rejected", "rejected"]);
    expect(secondStarted).toBe(false);
    // A new connection does not wait for or reuse the old queue.
    await expect(new BleOperationQueue().run("new", async () => 3)).resolves.toBe(3);
  });

  it("keeps the queue usable after an ordinary failure", async () => {
    const queue = new BleOperationQueue();
    await expect(queue.run("failure", async () => { throw new Error("failure"); })).rejects.toThrow("failure");
    await expect(queue.run("next", async () => 1)).resolves.toBe(1);
  });

  it("freezes the connection after a timeout instead of overlapping the stuck OS call", async () => {
    const queue = new BleOperationQueue(() => undefined, 20);
    const first = queue.run("stuck", () => new Promise<never>(() => undefined));
    let secondStarted = false;
    const second = queue.run("next", async () => { secondStarted = true; });
    const results = await Promise.allSettled([first, second]);
    expect(results[0]).toMatchObject({ status: "rejected", reason: { name: "TimeoutError" } });
    expect(results[1]).toMatchObject({ status: "rejected", reason: { name: "AbortError" } });
    expect(secondStarted).toBe(false);
  });
});
