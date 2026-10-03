CREATE TABLE date_table(id int, u_date date);
CREATE UNIQUE INDEX index_id on date_table(u_date);
INSERT INTO date_table VALUES (12,'20780119');