package app;

import app.db.*;
import app.service.AuthService;
import app.ui.LoginFrame;
import javax.swing.SwingUtilities;

public class Main {
    /**
     *  java -cp out app.Main                          -> mock data (no engine needed)
     *  java -cp out app.Main engine ../cyberdb ../cyberdb.db   -> the real C engine
     */
    public static void main(String[] args) throws Exception {
        Database db;
        if (args.length >= 3 && args[0].equals("engine")) {
            db = new ProcessDatabase(args[1], args[2]);
            Runtime.getRuntime().addShutdownHook(new Thread(db::close));
        } else {
            db = new MockDatabase();
        }
        AuthService auth = new AuthService(db);
        SwingUtilities.invokeLater(() -> new LoginFrame(auth, db).setVisible(true));
    }
}
