// Cross two frame boundaries before acknowledging that the first mounted view can paint.
export function afterFirstPaint(ready: () => void): () => void {
  let second: number | undefined;
  const first = requestAnimationFrame(() => {
    second = requestAnimationFrame(ready);
  });
  return () => {
    cancelAnimationFrame(first);
    if (second !== undefined) cancelAnimationFrame(second);
  };
}
