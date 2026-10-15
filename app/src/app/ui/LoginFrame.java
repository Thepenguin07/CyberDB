package app.ui;
import app.model.User;
import app.service.AuthService;
import app.db.Database;
import javax.swing.*;
import java.awt.*;
public class LoginFrame extends JFrame{
    public LoginFrame(AuthService auth,Database db){
        setTitle("CyberCafe Login");
        setSize(350,200);
        setDefaultCloseOperation(EXIT_ON_CLOSE);
        setLocationRelativeTo(null);
        JTextField nameField=new JTextField();
        JPasswordField passField=new JPasswordField();
        JButton loginBtn=new JButton("Login");
        JPanel panel=new JPanel(new GridLayout(3,2,10,10));
        panel.setBorder(BorderFactory.createEmptyBorder(20,20,20,20));
        panel.add(new JLabel("Username"));
        panel.add(nameField);
        panel.add(new JLabel("Password"));
        panel.add(passField);
        panel.add(new JLabel(""));
        panel.add(loginBtn);
        add(panel);
        loginBtn.addActionListener(e -> {
            try {
                String pw = new String(passField.getPassword());
                User u = auth.login(nameField.getText().trim(), pw);
                if (u == null) {
                    JOptionPane.showMessageDialog(this, "Wrong username or password");
                } else {
    dispose();
    new DashboardFrame(u, db, auth).setVisible(true);
} 
            } catch (Exception ex) {
                JOptionPane.showMessageDialog(this, "Error: " + ex.getMessage());
            }
        });
    }
}
