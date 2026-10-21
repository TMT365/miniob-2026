-- source /home/tmt/miniob/test/case/sql/function.sql
-- python3 test/case/miniob_test.py --test-cases=primary-function --skip-compile
DROP DATABASE IF EXISTS miniob_test;
CREATE DATABASE miniob_test;
USE miniob_test;

CREATE TABLE function_table(id int, name char(20), score float, u_date date);
CREATE TABLE function_table_2(id int, name char(30), score float, u_date date);
insert into function_table VALUES (1, 'UHWES37X', 3.36, '2021-09-14');
insert into function_table VALUES (7, '2E1R01HW', 9.98, '2022-11-10');
insert into function_table VALUES (9, 'XXGC', 9.86, '2016-05-31');

insert into function_table_2 VALUES (1, 'readf', 23.132123, '2111-09-14');
insert into function_table_2 VALUES (4, 'sedaeee', -0.3234, '3234-11-10');
insert into function_table_2 VALUES (99, 'eweax', 9.99311, '1256-05-31');

select t1.id, t1.name, round(t1.score) as score1, t2.id, t2.name, round(t2.score) as score2 from function_table t1, function_table_2 t2 where t1.id > t2.id and round(t1.score) > round(t2.score);
exit