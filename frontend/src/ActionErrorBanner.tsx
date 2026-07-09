type ActionErrorBannerProps = {
  error: string | null;
  onDismiss: () => void;
};

export function ActionErrorBanner({ error, onDismiss }: ActionErrorBannerProps) {
  if (!error) {
    return null;
  }

  return (
    <div className="action-error-banner" role="alert">
      <p>{error}</p>
      <button type="button" className="ghost-button" onClick={onDismiss}>
        Dismiss
      </button>
    </div>
  );
}
