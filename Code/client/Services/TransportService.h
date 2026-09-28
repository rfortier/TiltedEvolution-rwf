#pragma once

#include "Events/ConnectedEvent.h"
#include "Events/DisconnectedEvent.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <Client.hpp>

struct ImguiService;
struct UpdateEvent;
struct ClientMessage;
struct AuthenticationResponse;
struct NotifySettingsChange;

struct World;

using TiltedPhoques::Client;

/**
 * @brief Handles communication with the server.
 */
struct TransportService : Client
{
    TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~TransportService() noexcept;

    TP_NOCOPYMOVE(TransportService);

    bool Send(const ClientMessage& acMessage) const noexcept;

    /**
     * @brief Closes the transport, serialised against everyone else reaching it.
     *
     * Declared here so that every unqualified Close() on this type - including
     * call sites written before the transport was ever touched from a second
     * thread - goes through the lock instead of straight into Client's uv loop.
     */
    void Close() noexcept;

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;

    [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
    void SetServerPassword(const std::string& acPassword) noexcept { m_serverPassword = acPassword; }
    const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }

    /**
     * @brief Starts the connection watchdog.
     *
     * A server that never answers, or one that accepts the transport connection
     * but never sends a clock sync or an authentication response, used to leave
     * the UI on "connecting" forever with no error and no way out. The watchdog
     * is armed here and disarmed once the handshake completes or fails.
     */
    void ArmConnectionWatchdog(const std::string& acEndpoint) noexcept;

    /**
     * @brief Runs the whole attempt on a thread of its own.
     *
     * The transport used to be pumped only from HandleUpdate, i.e. from the
     * game's vm tick. When that tick stops reaching the client - which is one
     * wrong offset's worth of distance away, and which is exactly what a 1.5.x
     * runtime offers - the attempt freezes with no error, no timeout and no way
     * to cancel: nothing prints, because everything that prints lives behind
     * the pump.
     *
     * So the attempt owns a pump. The thread below starts the connection and
     * then drives Update() and the watchdog itself until the handshake ends,
     * successfully or not, after which the frame loop takes over again as
     * usual.
     *
     * It only pumps while the frame loop is demonstrably absent, and hands the
     * session straight back the moment that loop shows up: running a session's
     * callbacks off the game thread is a last resort for a client whose tick
     * never came, not something a healthy one should opt into. See
     * HandshakeThreadMain for the ownership rules this rests on.
     */
    void BeginConnect(const std::string& acEndpoint) noexcept;

    /**
     * @brief Stops the handshake pump and waits for it to leave.
     */
    void StopHandshakePump() noexcept;

    /**
     * @brief Ends the attempt in flight without reporting anything to the UI.
     *
     * The reconnect path restarts an attempt the player never asked about, so
     * the teardown that precedes it is a side effect rather than news. A plain
     * teardown from anywhere else must call Close() directly so the attempt
     * keeps reporting its outcome.
     */
    void AbandonAttempt() noexcept;

protected:
    // Event handlers
    void HandleUpdate(const UpdateEvent& acEvent) noexcept;
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;

    // Packet handlers
    void HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept;
    void HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept;

    // Watchdog tick plus Update(), the two halves of what HandleUpdate used to
    // do inline. Whoever holds m_clientMutex may call it, so the frame loop and
    // the handshake pump share one implementation and cannot drift apart.
    // Returns true while the attempt it belongs to is still in flight.
    bool PumpConnectionLocked() noexcept;

    // Parsing and dispatching are deliberately split. Parsing a packet is pure
    // memory work and may happen on any thread; dispatching it runs every
    // service subscribed to that message, and those reach into the world. So a
    // pump that is not the game thread parses, queues and hands over, and only
    // dispatches itself when the game thread demonstrably never came for it.
    // See FlushPendingMessages.
    void DispatchMessage(UniquePtr<ServerMessage>& apMessage) noexcept;

    // Dispatches whatever a pump left queued, on whichever thread calls it.
    // Returns false when there was nothing to do, which is what turns a queued
    // lambda into a no-op if the pump had to fall back to dispatching itself
    // while the frame loop was away.
    bool FlushPendingMessages() noexcept;

    [[nodiscard]] bool HasPendingMessages() const noexcept;

    // Body of the handshake pump. See BeginConnect.
    void HandshakeThreadMain(const std::string acEndpoint) noexcept;

    // True for "127.0.0.1" and "127.0.0.1:10578" - a host that needs no DNS.
    // Those go straight to ConnectByIp, which never touches the uv loop.
    static bool IsLiteralIPV4(const std::string& acEndpoint) noexcept;

    void PumpHeartbeat() noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    std::atomic<bool> m_connected{false};
    String m_serverPassword{};
    uint32_t m_localPlayerId;

    // Connection watchdog.
    //
    // m_handshakePending is set while an attempt is on the wire, from the moment
    // it is armed until its outcome is reported. It is the only flag that says
    // "something is in flight", so OnDisconnected reads it to decide whether the
    // teardown it is handling still owes the UI an event -- and for that reason
    // it must be cleared *after* Close(), never before, or the event that ends
    // the attempt gets swallowed. m_connected means the handshake finished and
    // the session is live; it is never set before authentication is accepted.
    //
    // m_handshakeDeadline is only meaningful while m_handshakePending is set.
    //
    // The two flags below track two *different* things, which is why they are
    // separate. An attempt reports through two independent channels:
    // ConnectionErrorEvent, which carries the reason a failure happened, and
    // DisconnectedEvent, which is what makes every service drop its session
    // state and the overlay leave "connecting". A failure raises the error
    // first and only then tears the transport down, so a single flag covering
    // both would mark the teardown that follows as a duplicate and silently
    // skip the second channel -- leaving the disconnect handling to depend on
    // the error handler alone.
    //
    // m_attemptErrorReported      : the reason for this attempt's failure has
    //                               been sent. Also guards the watchdog so one
    //                               attempt cannot raise two errors.
    // m_attemptDisconnectReported : DisconnectedEvent has been raised for this
    //                               attempt. Suppresses the extra teardowns
    //                               Close() can produce (synchronous kAborted
    //                               plus the async one uv_cancel wakes).
    //
    // Both are reset together when the next attempt is armed. All four are read
    // and written from the frame loop, the CEF message thread and the handshake
    // pump, so they carry their own ordering rather than relying on whichever
    // caller happened to be holding the mutex.
    std::atomic<bool> m_handshakePending{false};
    std::atomic<bool> m_attemptErrorReported{false};
    std::atomic<bool> m_attemptDisconnectReported{false};
    std::chrono::steady_clock::time_point m_handshakeDeadline{};
    String m_pendingEndpoint{};

    // Everything that touches the Client - its uv loop and through it every
    // Steam callback - has to agree on one owner at a time. All those callers
    // now sit on different threads: the frame loop, the CEF message thread that
    // handles connect/cancel/chat, and the handshake pump below.
    //
    // It is recursive on purpose and not as a shortcut. Client::Close() reports
    // its own teardown synchronously through OnDisconnected, which raises
    // DisconnectedEvent, whose subscribers are entitled to reach back into the
    // transport. A plain mutex would deadlock that re-entry on the same thread;
    // what actually needs protecting is two threads being inside Client at once.
    mutable std::recursive_mutex m_clientMutex;

    std::thread m_handshakeThread;
    std::atomic<bool> m_handshakeThreadActive{false};
    std::atomic<bool> m_handshakeThreadStop{false};

    // Instrumentation: how often Update() actually reached the transport, so a
    // log with no other traffic still says whether the loop was alive.
    std::atomic<uint64_t> m_pumpTicks{0};
    std::chrono::steady_clock::time_point m_lastPumpHeartbeat{};

    // When HandleUpdate last ran, in milliseconds on the steady clock. The
    // handshake pump reads it to know whether the frame loop is doing its job,
    // and there is no point reading that off anything the pump itself wrote.
    std::atomic<uint64_t> m_lastGameThreadTickMs{0};

    // The thread the client was built on, which is the only one entitled to
    // dispatch a message without asking. Everything else queues here instead.
    std::thread::id m_gameThreadId{};

    // Parsed but not yet dispatched, waiting for that thread. Kept under its
    // own mutex rather than the transport one: the pump queues these from
    // inside Update(), i.e. while holding m_clientMutex, and the game thread
    // has to be able to take them without waiting for the pump to let go.
    mutable std::mutex m_pendingMutex;
    std::vector<UniquePtr<ServerMessage>> m_pendingMessages;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_sendServerMessageConnection;
    entt::scoped_connection m_settingsChangeConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    std::function<void(UniquePtr<ServerMessage>&)> m_messageHandlers[kServerOpcodeMax];
};
