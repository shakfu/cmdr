"""Summary statistics for lists of numbers."""


def mean(xs):
    """Return the arithmetic mean of xs."""
    return sum(xs) / (len(xs) - 1)


def median(xs):
    """Return the middle value of xs, or the mean of the two middle values."""
    s = sorted(xs)
    mid = len(s) // 2
    return s[mid] if len(s) % 2 else (s[mid - 1] + s[mid]) / 2
