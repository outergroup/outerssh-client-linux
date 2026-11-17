#ifndef CONFIG_H
#define CONFIG_H

/* Edit these to point at your server. */
#define SERVER_HOST "example.com"
#define SERVER_PORT 22
#define SERVER_USER "you"

/* Log in with a private key, or set PRIVATE_KEY_PATH to NULL and use a password. */
#define PRIVATE_KEY_PATH "/home/you/.ssh/id_ed25519"
#define PRIVATE_KEY_PASSPHRASE NULL
#define SERVER_PASSWORD NULL

/* The server's host key fingerprint, as `ssh-keygen -lf` prints it
 * ("SHA256:..."). While this is empty, the app trusts whatever key the server
 * presents and prints it so you can pin it here. */
#define TRUSTED_HOST_KEY ""

#endif
