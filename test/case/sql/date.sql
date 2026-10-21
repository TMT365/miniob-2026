-- source /home/tmt/miniob/test/case/sql/date.sql
-- python3 test/case/miniob_test.py --test-case=primary-date --skip-compile
DROP DATABASE IF EXISTS miniob_test;
CREATE DATABASE miniob_test;
USE miniob_test;

CREATE TABLE date_table(id int, u_date date);
CREATE INDEX index_id ON date_table(u_date);

INSERT INTO date_table VALUES (1,'2020-01-21');
INSERT INTO date_table VALUES (2,'2020-10-21');
INSERT INTO date_table VALUES (3,'2020-1-01');
INSERT INTO date_table VALUES (4,'2020-01-1');
INSERT INTO date_table VALUES (5,'2019-12-21');
INSERT INTO date_table VALUES (6,'2016-2-29');
INSERT INTO date_table VALUES (7,'1970-1-1');
INSERT INTO date_table VALUES (8,'2000-01-01');
INSERT INTO date_table VALUES (9,'2038-1-19');
INSERT INTO date_table VALUES (10,'18-1-19');
INSERT INTO date_table VALUES (11,'70-10-19');
INSERT INTO date_table VALUES (12,'20780119');
INSERT INTO date_table VALUES (13,'281119');

-- 时间比较
SELECT * FROM date_table WHERE u_date>'2020-1-20';
SELECT * FROM date_table WHERE u_date<'2019-12-31';
SELECT * FROM date_table WHERE u_date='2020-1-1';
-- 删除时间大于2012-2-29的记录
DELETE FROM date_table WHERE u_date>'2012-2-29';

SELECT * FROM date_table;
SELECT * FROM date_table WHERE u_date='2017-2-29';
SELECT * FROM date_table WHERE u_date='2017-21-29';
SELECT * FROM date_table WHERE u_date='2017-12-32';
SELECT * FROM date_table WHERE u_date='2017-11-31';

INSERT INTO date_table VALUES (10,'2017-2-29');
INSERT INTO date_table VALUES (11,'2017-21-29');
INSERT INTO date_table VALUES (12,'2017-12-32');
INSERT INTO date_table VALUES (13,'2017-11-31');