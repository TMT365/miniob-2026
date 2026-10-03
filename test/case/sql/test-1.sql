CREATE TABLE date_table_1(id int, u_date date);
CREATE TABLE date_table_2(id int, u_date date);
INSERT INTO date_table_1 VALUES (1,'2006-1-10');
INSERT INTO date_table_1 VALUES (2,'2008-10-10');
INSERT INTO date_table_2 VALUES (1,'2009-2-18');
INSERT INTO date_table_2 VALUES (2,'2018-9-9');
SELECT * FROM date_table_1 , date_table_2 WHERE date_table_2.id = date_table_1.id;
calc 1, 2, 3;