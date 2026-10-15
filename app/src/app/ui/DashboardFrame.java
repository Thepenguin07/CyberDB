package app.ui;
import app.db.Database;
import app.model.User;
import app.service.AuthService;
import javax.swing.*;
import java.awt.*;
public class DashboardFrame extends JFrame{
     public DashboardFrame(User user, Database db, AuthService auth) {
        setTitle("CyberCafe - " + user.name + " (" + user.role + ")");
        setSize(400, 400);
        setDefaultCloseOperation(EXIT_ON_CLOSE);
        setLocationRelativeTo(null);

        JPanel panel = new JPanel(new GridLayout(0, 1, 10, 10));
        panel.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
        panel.add(new JLabel("Welcome, " + user.name + "   Balance: Rs " + user.balance));

        if (user.canBook()) {
            JButton book = new JButton("Book a PC");
            book.addActionListener(e -> new PcBookingFrame(user, db).setVisible(true));
            panel.add(book);
        }
        if (user.canManageBilling()) {
            addButton(panel, "Manage Billing");
        }
        if (user.canViewReports()) {
            addButton(panel, "Reports");
        }
        if (user.canManageUsers()) {
            addButton(panel, "Manage Users");
        }

        JButton logout = new JButton("Logout");
        logout.addActionListener(e -> {
            dispose();
            new LoginFrame(auth, db).setVisible(true);
        });
        panel.add(logout);

        add(panel);
    }

    private void addButton(JPanel panel, String text) {
        JButton b = new JButton(text);
        b.addActionListener(e ->
            JOptionPane.showMessageDialog(this, text + " - coming soon"));
        panel.add(b);
}
}