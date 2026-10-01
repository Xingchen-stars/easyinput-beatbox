export type EditBank = 0 | 1 | 2;

function normalizeVariation(variation: number): 0 | 1 {
  return variation ? 1 : 0;
}

/**
 * Follow a device-originated A/B change without pulling the editor away from
 * Fill on unrelated periodic state packets.
 */
export function syncEditBankFromVariation(
  current: EditBank,
  previousVariation: number,
  nextVariation: number,
): EditBank {
  const previous = normalizeVariation(previousVariation);
  const next = normalizeVariation(nextVariation);
  return previous === next ? current : next;
}
