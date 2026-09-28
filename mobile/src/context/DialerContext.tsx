import {
  createContext,
  type PropsWithChildren,
  useContext,
  useMemo,
  useState,
} from "react";

export type Subscriber = {
  id: "A" | "B";
  name: string;
  number: string;
  initials: string;
};

export const SUBSCRIBERS: Subscriber[] = [
  {
    id: "A",
    name: "Subscriber A",
    number: "0712 000 001",
    initials: "A",
  },
  {
    id: "B",
    name: "Subscriber B",
    number: "0712 000 002",
    initials: "B",
  },
];

type DialerContextValue = {
  activeSubscriber: Subscriber | null;
  peerSubscriber: Subscriber | null;
  selectSubscriber: (subscriber: Subscriber) => void;
  resetSubscriber: () => void;
};

const DialerContext =
  createContext<DialerContextValue | null>(null);

export function DialerProvider({
  children,
}: PropsWithChildren) {
  const [
    activeSubscriber,
    setActiveSubscriber,
  ] = useState<Subscriber | null>(null);

  const peerSubscriber = useMemo(() => {
    if (!activeSubscriber) {
      return null;
    }

    return (
      SUBSCRIBERS.find(
        (subscriber) =>
          subscriber.id !== activeSubscriber.id,
      ) ?? null
    );
  }, [activeSubscriber]);

  const value = useMemo(
    () => ({
      activeSubscriber,
      peerSubscriber,
      selectSubscriber:
        (subscriber: Subscriber) =>
          setActiveSubscriber(subscriber),
      resetSubscriber: () =>
        setActiveSubscriber(null),
    }),
    [activeSubscriber, peerSubscriber],
  );

  return (
    <DialerContext.Provider value={value}>
      {children}
    </DialerContext.Provider>
  );
}

export function useDialer() {
  const value = useContext(DialerContext);

  if (!value) {
    throw new Error(
      "useDialer must be used inside DialerProvider",
    );
  }

  return value;
}
