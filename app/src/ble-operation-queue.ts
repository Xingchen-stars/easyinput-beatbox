/** One queue per GATT connection. A whole fragmented message is one operation. */
export class BleOperationQueue {
  private tail: Promise<void> = Promise.resolve();
  private active = true;
  private sequence = 0;

  constructor(
    private readonly diagnostic: (message: string) => void = () => undefined,
    private readonly timeoutMs = 12_000,
  ) {}

  get isActive() { return this.active; }

  invalidate() { this.active = false; }

  private assertActive() {
    if (!this.active) {
      throw Object.assign(new Error("Bluetooth connection was closed; queued operation cancelled."), {
        name: "AbortError",
      });
    }
  }

  run<T>(label: string, operation: () => Promise<T>): Promise<T> {
    const sequence = ++this.sequence;
    const result = this.tail.then(async () => {
      this.assertActive();
      const started = Date.now();
      this.diagnostic(`gatt-op:${sequence}:start ${label}`);
      let timer: ReturnType<typeof setTimeout> | undefined;
      try {
        const timeout = new Promise<never>((_resolve, reject) => {
          timer = setTimeout(() => {
            // A timeout does not mean the OS stopped the operation. Freeze this
            // connection's queue; the caller disconnects before a fresh attempt.
            this.invalidate();
            reject(Object.assign(new Error(`Bluetooth operation timed out (${label}).`), { name: "TimeoutError" }));
          }, this.timeoutMs);
        });
        const value = await Promise.race([operation(), timeout]);
        this.assertActive();
        this.diagnostic(`gatt-op:${sequence}:ok ${label} duration_ms=${Date.now() - started}`);
        return value;
      } catch (error) {
        this.diagnostic(`gatt-op:${sequence}:error ${label}`);
        throw error;
      } finally {
        clearTimeout(timer);
      }
    });
    // A rejected operation must not create an unhandled rejection in the queue tail.
    this.tail = result.then(() => undefined, () => undefined);
    return result;
  }
}
