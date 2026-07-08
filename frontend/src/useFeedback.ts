import { useState } from "react";

export const useFeedback = () => {
  const [labelStatus, setLabelStatus] = useState<string | null>(null);
  const [labelStatusWarning, setLabelStatusWarning] = useState(false);
  const [actionError, setActionError] = useState<string | null>(null);

  return {
    actionError,
    labelStatus,
    labelStatusWarning,
    setActionError,
    setLabelStatus,
    setLabelStatusWarning,
  };
};
