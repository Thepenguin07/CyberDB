package app.ui;
import app.db.Database;
import app.model.User;
import javax.swing.*;
import java.awt.*;
import java.util.HashMap;
import java.util.Map;
public class PcBookingFrame extends JFrame {
    private static final int RATE_PER_MINUTE = 2;   // Rs
    private static final Map<Integer, Long> startTimes = new HashMap<>();
    private static final Map<Integer, Integer> usedBy = new HashMap<>();

    private final Database db;
    private final User user;
    private final JPanel grid = new JPanel(new GridLayout(2, 3, 10, 10));

    public PcBookingFrame(User user, Database db) {
        this.user = user;
        this.db = db;
        setTitle("Book a PC");
        setSize(450, 300);
        setDefaultCloseOperation(DISPOSE_ON_CLOSE);
        setLocationRelativeTo(null);
        grid.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
        add(grid);
        refresh();
    }

    private void refresh() {
        grid.removeAll();
        try {
            for (String[] r : db.query("SELECT * FROM pcs;")) {
                int id = Integer.parseInt(r[0]);
                boolean free = r[2].equals("FREE");
                JButton b = new JButton(r[1] + (free ? " - FREE" : " - BUSY"));
                b.setOpaque(true);
                b.setBorderPainted(false);   // needed so colours show on Mac
                b.setBackground(free ? Color.GREEN : Color.RED);
                b.addActionListener(e -> onClick(id, free));
                grid.add(b);
            }
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, "Error: " + ex.getMessage());
        }
        grid.revalidate();
        grid.repaint();
    }

    private void onClick(int id, boolean free) {
        try {
            if (free) {
                db.execute("UPDATE pcs SET status = 'BUSY' WHERE id = " + id + ";");
                startTimes.put(id, System.currentTimeMillis());
                usedBy.put(id, user.id);
            } else {
                Integer who = usedBy.get(id);
                boolean mine = who != null && who == user.id;
                if (!mine && !user.canManageBilling()) {
                    JOptionPane.showMessageDialog(this, "This PC is in use by another user.");
                    return;
                }
                Long start = startTimes.get(id);
                long ms = start == null ? 0 : System.currentTimeMillis() - start;
                int minutes = (int) Math.max(1, Math.ceil(ms / 60000.0));
                int bill = minutes * RATE_PER_MINUTE;
                db.execute("UPDATE pcs SET status = 'FREE' WHERE id = " + id + ";");
                startTimes.remove(id);
                usedBy.remove(id);
                JOptionPane.showMessageDialog(this,
                    "Session ended.\nMinutes: " + minutes + "\nBill: Rs " + bill);
            }
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, "Error: " + ex.getMessage());
        }
        refresh();
    }
}