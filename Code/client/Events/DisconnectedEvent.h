#pragma once

/**
 * @brief Dispatched when the client disconnects from the server.
 */
struct DisconnectedEvent
{
    DisconnectedEvent() {}

    /**
     * @brief Whether the session ended because the connection attempt failed.
     *
     * True when the attempt was already known to have failed before the
     * transport went down, i.e. an error was reported for it. The overlay uses
     * this to tell "could not connect, offer to retry" apart from an ordinary
     * disconnect from a session that had been running.
     */
    DisconnectedEvent(bool aIsError) noexcept
        : IsError(aIsError)
    {
    }

    bool IsError{false};
};
