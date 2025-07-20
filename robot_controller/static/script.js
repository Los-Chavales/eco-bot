document.addEventListener('DOMContentLoaded', () => {
    const buttons = document.querySelectorAll('.control-btn');
    const statusBar = document.getElementById('status-bar');

    const sendCommand = async (command) => {
        statusBar.textContent = `Enviando: ${command}...`;
        statusBar.className = 'status'; // Reset class

        try {
            const response = await fetch('/send_command', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ command: command }),
            });

            const result = await response.json();

            if (response.ok) {
                statusBar.textContent = `Éxito: ${result.message}`;
                statusBar.classList.add('success');
            } else {
                statusBar.textContent = `Error: ${result.message}`;
                statusBar.classList.add('error');
            }

        } catch (error) {
            statusBar.textContent = 'Error de red. ¿Servidor está caído?';
            statusBar.classList.add('error');
            console.error('Error al enviar el comando:', error);
        }
    };

    buttons.forEach(button => {
        button.addEventListener('click', () => {
            const command = button.dataset.command;
            sendCommand(command);
        });
    });
});