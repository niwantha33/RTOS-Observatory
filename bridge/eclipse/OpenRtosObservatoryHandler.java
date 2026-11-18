// SPDX-License-Identifier: Apache-2.0
// Copy this class into the ZEW UI plug-in under its normal package.
// This is an opt-in offline report handoff, NOT a live GDB or trace bridge.
package io.github.niwantha33.zew.ui;

import java.io.IOException;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Base64;

import org.eclipse.core.commands.AbstractHandler;
import org.eclipse.core.commands.ExecutionEvent;
import org.eclipse.core.commands.ExecutionException;
import org.eclipse.debug.core.ILaunch;
import org.eclipse.debug.core.model.IDebugElement;
import org.eclipse.debug.ui.DebugUITools;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.swt.SWT;
import org.eclipse.swt.program.Program;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.handlers.HandlerUtil;

/**
 * Hands an already-produced report to RTOS Observatory without touching target state.
 * Does not open another GDB/OpenOCD connection, halt, resume, read memory or poll registers.
 * Explicitly requires RTOS_OBSERVATORY_HOME pointing to the installed package root.
 * Eclipse plugin registration is provided separately in plugin-extensions.xml.
 */
public final class OpenRtosObservatoryHandler extends AbstractHandler {
    private static final long MAX_REPORT = 30L * 1024 * 1024;

    @Override
    public Object execute(ExecutionEvent event) throws ExecutionException {
        var shell = HandlerUtil.getActiveShellChecked(event);
        final String home = System.getenv("RTOS_OBSERVATORY_HOME");
        if (home == null || home.isBlank()) {
            MessageDialog.openWarning(shell, "RTOS Observatory",
                    "Set RTOS_OBSERVATORY_HOME to the extracted rtos-observatory directory first.");
            return null;
        }
        final Path viewer = Path.of(home).resolve("viewer").resolve("index.html");
        if (!Files.isRegularFile(viewer)) {
            MessageDialog.openError(shell, "RTOS Observatory", "Cannot find viewer/index.html in RTOS_OBSERVATORY_HOME");
            return null;
        }
        var chooser = new FileDialog(shell, SWT.OPEN);
        chooser.setText("Open RTOS Observatory Report (offline JSON)");
        chooser.setFilterExtensions(new String[] {"*.json"});
        var selected = chooser.open();
        if (selected == null) return null;
        try {
            final Path report = Path.of(selected).toAbsolutePath().normalize();
            long length = Files.size(report);
            if (length <= 0 || length > MAX_REPORT)
                throw new IOException("Report must be between 1 byte and 30 MiB");
            // Avoid executing arbitrary report text as JavaScript. Pass only Base64 data
            // to an external script, and decode using JSON.parse in the viewer process.
            final String encoded = Base64.getEncoder().encodeToString(Files.readAllBytes(report));
            final Path session = Files.createTempDirectory("zew-rtos-observatory-");
            Files.copy(viewer, session.resolve("index.html"), StandardCopyOption.REPLACE_EXISTING);
            final String loader = "'use strict';\n" +
                    "window.DEMO_REPORT = JSON.parse(new TextDecoder().decode(" +
                    "Uint8Array.from(atob('" + encoded + "'), c => c.charCodeAt(0))));\n";
            Files.writeString(session.resolve("demo-report.js"), loader, StandardCharsets.UTF_8);
            // Optional launch metadata is informative only, and deliberately minimal.
            final ILaunch launch = selectedLaunch();
            final String launchText = launch == null ? "No selected Eclipse debug session" :
                    "Selected Eclipse launch (" + launch.getLaunchMode() + ")";
            Files.writeString(session.resolve("handoff.txt"),
                    "rtos-observatory-handoff/v1\n" + launchText + "\n" +
                    "NO LIVE DEBUG ACCESS; report is offline\n", StandardCharsets.UTF_8);
            final URI url = session.resolve("index.html").toUri();
            if (!Program.launch(url.toString())) {
                MessageDialog.openInformation(shell, "RTOS Observatory",
                        "Open this viewer in your browser:\n" + url);
            }
        } catch (IOException | RuntimeException failure) {
            MessageDialog.openError(shell, "RTOS Observatory", "Could not open report: " + failure.getMessage());
        }
        return null;
    }

    private static ILaunch selectedLaunch() {
        var workbench = PlatformUI.getWorkbench();
        var window = workbench.getActiveWorkbenchWindow();
        if (window == null) return null;
        ISelection context = DebugUITools.getDebugContextManager()
                .getContextService(window).getActiveContext();
        if (!(context instanceof IStructuredSelection selection)) return null;
        Object element = selection.getFirstElement();
        if (element instanceof ILaunch launch) return launch;
        if (element instanceof IDebugElement debugElement) return debugElement.getLaunch();
        return null;
    }
}
