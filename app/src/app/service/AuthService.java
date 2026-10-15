package app.service;
import app.db.Database;
import app.model.User;
import app.util.PasswordUtil;
import java.util.List;
public class AuthService {
    private Database db;
    public AuthService(Database db){
        this.db=db;
    }
    public User login(String name,String password) throws Exception{
        if(name.contains("'"))return null;
        List<String[]> rows=db.query("Select * from users where name = '"+name+"';");
        String hashed=PasswordUtil.hash(password);
        for(String[] r:rows){
            if(r[1].equals(name)&&r[2].equals(hashed)){
                return new User(Integer.parseInt(r[0]),r[1],User.Role.valueOf(r[3]),Integer.parseInt(r[4]));
            }
        }return null;
    }
}

