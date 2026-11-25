package com.vertexforge.launcher.instance;

import java.io.IOException;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.file.Path;

/**
 * Unix-domain sockets for the single-instance channel (VK-1685); a seam so that tests can stand in for a Windows
 * without them. They need Windows 10 1803 or later: before that, opening one throws
 * {@link UnsupportedOperationException}. A socket path longer than about 106 UTF-8 bytes makes bind and connect throw
 * a {@link java.net.SocketException}.
 */
public interface LocalSockets {

    /** A blocking server bound to {@code socketFile}, which must not exist yet. */
    ServerSocketChannel bind(Path socketFile) throws IOException;

    /** A blocking channel connected to the server bound to {@code socketFile}. */
    SocketChannel connect(Path socketFile) throws IOException;

    /** The JDK's Unix-domain sockets. */
    static LocalSockets system() {
        return new LocalSockets() {
            @Override
            public ServerSocketChannel bind(Path socketFile) throws IOException {
                ServerSocketChannel server = ServerSocketChannel.open(StandardProtocolFamily.UNIX);
                try {
                    server.bind(UnixDomainSocketAddress.of(socketFile));
                    return server;
                } catch (IOException | RuntimeException e) {
                    try {
                        server.close();
                    } catch (IOException closeFailure) {
                        e.addSuppressed(closeFailure);
                    }
                    throw e;
                }
            }

            @Override
            public SocketChannel connect(Path socketFile) throws IOException {
                return SocketChannel.open(UnixDomainSocketAddress.of(socketFile));
            }
        };
    }
}
