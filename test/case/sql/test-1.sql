
CREATE TABLE date_table(id int, u_date date);
CREATE INDEX index_id on date_table(u_date);
INSERT INTO date_table VALUES (1,'2020-01-21');
INSERT INTO date_table VALUES (2,'2020-10-21');
INSERT INTO date_table VALUES (3,'2020-01-01');
INSERT INTO date_table VALUES (10,'1950-02-02');
INSERT INTO date_table VALUES (6,'2016-02-29');
INSERT INTO date_table VALUES (7,'1970-01-01');

SELECT * FROM date_table WHERE u_date!='2000-01-01' and u_date < '2019-12-21';
SELECT * FROM date_table WHERE u_date<>'2000-01-01' and u_date < '2019-12-21';
