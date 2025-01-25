# Deploying openleadr-rs on AWS Lightsail

This README provides instructions for deploying the `openleadr-rs` repository, which implements an OpenADR 3.0 VTN server, on AWS Lightsail. Lightsail offers a simple and affordable way to run virtual private servers, making it suitable for deploying this project.

## Prerequisites

Before you begin, ensure you have the following:

*   **An AWS Account:** You'll need an active AWS account to use Lightsail.
*   **AWS Lightsail Instance:** You should have an AWS Lightsail instance running. It's recommended to use a Linux-based instance (e.g., Ubuntu) as it simplifies Docker installation. A Lightsail instance with at least 1GB of RAM is recommended.
*   **SSH Client:** You'll need an SSH client (like PuTTY for Windows, or the built-in `ssh` command on macOS/Linux) to connect to your Lightsail instance.

## Steps to Deploy on AWS Lightsail

Follow these steps to deploy `openleadr-rs` on your AWS Lightsail instance:

1.  **Connect to your Lightsail Instance via SSH:**

    Use your SSH client to connect to the public IP address of your Lightsail instance. Log in using the username and private key associated with your instance.

2.  **Install Docker and Docker Compose:**

    If Docker and Docker Compose are not already installed on your Lightsail instance, install them using the following commands. For Ubuntu, you can use:

    ```bash
    sudo apt-get update
    sudo apt-get install docker.io -y
    sudo apt-get install docker-compose-plugin -y

    # Verify installation:
    docker --version
    docker compose version
    ```

    Refer to the official Docker documentation for instructions on other Linux distributions if needed.

3.  **Clone the `openleadr-rs` Repository:**

    Clone the `openleadr-rs` repository to your Lightsail instance:

    ```bash
    git clone https://github.com/OpenLEADR/openleadr-rs.git
    cd openleadr-rs
    ```

4.  **Configure Environment Variables:**

    Copy the `docker-compose.yml` file and the `vtn.Dockerfile` to the root of the repository if they are not already present. The provided `docker-compose.yml` file in the repository root should be used as is.

    You can customize the VTN's behavior by setting environment variables in the `docker-compose.yml` file.  The following variables are commonly used and are already pre-configured in the provided `docker-compose.yml`:

    *   `VTN_PORT`:  The port the VTN server will listen on (default: `3000`). You can map this to port `80` or `443` on Lightsail for standard HTTP/HTTPS access if desired.
    *   `PG_PORT`: The port for the PostgreSQL database (default: `5432`).
    *   `PG_USER`, `PG_DB`, `PG_PASSWORD`: Database credentials for PostgreSQL.
    *   `PG_TZ`:  Timezone for PostgreSQL (e.g., `UTC`).
    *   `DATABASE_URL`: Connection string for the PostgreSQL database. This is automatically configured based on the other `PG_*` variables in the provided `docker-compose.yml`.

    You can modify these variables directly in the `docker-compose.yml` file using a text editor like `nano` or `vim`:

    ```bash
    nano docker-compose.yml
    ```

5.  **Start the Services with Docker Compose:**

    Navigate to the root directory of the cloned repository (where `docker-compose.yml` is located) and start the services using Docker Compose:

    ```bash
    docker compose up -d db # Start the database container in detached mode
    cargo sqlx migrate run  # Apply database migrations inside the VTN container
    docker compose up -d    # Start the VTN and other services in detached mode
    ```

    This command will:

    *   Start the PostgreSQL database container in detached mode (`-d`).
    *   Run database migrations using `cargo sqlx migrate run` to set up the database schema.
    *   Start the VTN server and any other services defined in `docker-compose.yml` in detached mode.

6.  **Access the openleadr-rs VTN Server:**

    Once the containers are running, you can access the openleadr-rs VTN server in your web browser using the public IP address of your Lightsail instance and the port you configured (or the default port `3000`).

    For example, if your Lightsail instance's public IP is `203.0.113.1` and you are using the default port, you would access it at:

    ```
    http://203.0.113.1:3000
    ```

    You can verify that the VTN is running by accessing the health check endpoint:

     ```
     http://203.0.113.1:3000/health
     ```
     It should return "OK".

## Configuration Notes

*   **Database Persistence:** The `docker-compose.yml` file configures a volume (`database-data`) to persist the PostgreSQL database data. This ensures that your data is preserved even if the container is stopped or restarted.
*   **Security:** For production deployments, consider the following security measures:
    *   **HTTPS:** Configure HTTPS using a reverse proxy like Nginx or Traefik with Let's Encrypt for SSL/TLS certificate management. This is beyond the scope of this basic deployment guide but is highly recommended.
    *   **Firewall:** Configure the Lightsail instance firewall to restrict access to necessary ports only (e.g., port `3000` or `443` for the VTN, and SSH port `22`).
    *   **Database Security:** For production, consider more robust PostgreSQL password management instead of `trust` based authentication.
*   **Resource Scaling:** If you anticipate high load, you might need to scale up your Lightsail instance to a larger size or consider using a more scalable AWS service like ECS or EKS for container orchestration.

This README provides a basic setup for deploying `openleadr-rs` on AWS Lightsail. For more advanced configurations, security best practices, and production deployments, please refer to the project documentation and consider consulting with AWS Lightsail and Docker best practices.
