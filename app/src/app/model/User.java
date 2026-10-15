package app.model;

public class User {
    public enum Role{
        ADMIN,STAFF,CUSTOMER}
        public int id;
        public String name;
        public Role role;
        public int balance;
        public User(int id,String name,Role role,int balance){
            this.id=id;
            this.name=name;
            this.role=role;
            this.balance=balance;
        }
          public boolean canBook()          { return true; }
    public boolean canManageBilling() { return role == Role.ADMIN || role == Role.STAFF; }
    public boolean canViewReports()   { return role == Role.ADMIN; }
    public boolean canManageUsers()   { return role == Role.ADMIN; }
}

