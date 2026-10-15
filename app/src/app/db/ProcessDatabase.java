package app.db;

import java.io.*;
import java.util.ArrayList;
import java.util.List;

/**
 * Talks to the C engine (the `cyberdb` program) over stdin/stdout.
 * Protocol: we send ONE command per line. The engine answers
 *     OK            or     ERR message
 *     row|row|...          (zero or more lines, columns separated by '|')
 *     END
 * Engine diagnostics (crash recovery messages) go to stderr and are shown in this console.
 */
public class ProcessDatabase implements Database {
    private final Process proc;
    private final BufferedWriter out;
    private final BufferedReader in;

    public ProcessDatabase(String enginePath, String dbFile) throws IOException {
        ProcessBuilder pb = new ProcessBuilder(enginePath, dbFile);
        pb.redirectError(ProcessBuilder.Redirect.INHERIT);
        proc = pb.start();
        out = new BufferedWriter(new OutputStreamWriter(proc.getOutputStream(), "UTF-8"));
        in = new BufferedReader(new InputStreamReader(proc.getInputStream(), "UTF-8"));
    }

    /** SELECT, SESSION LIST/STOP, STATS ... : returns the rows. */
    @Override
    public synchronized List<String[]> query(String sql) throws Exception {
        if (sql.indexOf('\n') >= 0) throw new IllegalArgumentException("one statement per call");
        out.write(sql);
        out.write("\n");
        out.flush();                                   // without this the engine never sees the line

        String status = in.readLine();
        if (status == null) throw new IOException("database engine stopped unexpectedly");

        List<String[]> rows = new ArrayList<>();
        String line;
        while ((line = in.readLine()) != null && !line.equals("END")) {
            rows.add(line.split("\\|", -1));           // -1 keeps empty trailing columns
        }
        if (line == null) throw new IOException("database engine stopped unexpectedly");
        if (!status.equals("OK")) throw new Exception(status.startsWith("ERR ") ? status.substring(4) : status);
        return rows;
    }

    /** INSERT, UPDATE, DELETE, BEGIN, COMMIT, ROLLBACK. */
    @Override
    public void execute(String sql) throws Exception {
        query(sql);
    }

    @Override
    public synchronized void close() {
        try {
            out.write("EXIT\n");
            out.flush();
            proc.waitFor();                            // engine checkpoints the log before exiting
        } catch (Exception ignored) {
            proc.destroy();
        }
    }
}
