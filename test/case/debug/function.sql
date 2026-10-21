-- CREATE TABLE FUNC_TABLE(ID INT, COL1 FLOAT, COL2 DATE, COL3 CHAR(9));
-- INSERT INTO FUNC_TABLE VALUES (1, 1.46, '2023-01-01', 'a9c');
-- INSERT INTO FUNC_TABLE VALUES (2, 2.50, '2026-01-01', 'd');
-- INSERT INTO FUNC_TABLE VALUES (3, 3.91, '2024-01-01', 'gh880');
-- INSERT INTO FUNC_TABLE VALUES (4, 4.99, '2025-01-01', '唐明涛');
--
-- SELECT ID,ROUND(COL1), DATE_FORMAT(COL2, '%Y-%M-%D'),LENGTH(COL3) FROM FUNC_TABLE;
-- SELECT ID,ROUND(COL1), DATE_FORMAT(COL2, '%y-%m-%d'),LENGTH(COL3) FROM FUNC_TABLE;
-- SELECT ID,ROUND(COL1), DATE_FORMAT(COL2, '%z-%n-%d'),LENGTH(COL3) FROM FUNC_TABLE;
-- SELECT ID, COUNT(*), SUM(COL1), AVG(COL1), DATE_FORMAT(COL2, '%z-%n-%d'), LENGTH(COL3) FROM FUNC_TABLE;

-- + FAILURE
-- below are some requests executed before(partial) --
-- init data
-- CREATE TABLE function_table(id int, name char(20), score float, u_date date);
-- CREATE TABLE function_table_2(id int, name char(30), score float, u_date date);
-- insert into function_table VALUES (1, 'UHWES37X', 3.36, '2021-09-14');
-- insert into function_table VALUES (7, '2E1R01HW', 9.98, '2022-11-10');
-- insert into function_table VALUES (9, 'XXGC', 9.86, '2016-05-31');
--
-- insert into function_table_2 VALUES (1, 'readf', 23.132123, '2111-09-14');
-- insert into function_table_2 VALUES (4, 'sedaeee', -0.3234, '3234-11-10');
-- insert into function_table_2 VALUES (99, 'eweax', 9.99311, '1256-05-31');
-- -- select t1.id, t1.name, round(t1.score) as sc
-- select t1.id, t1.name, round(t1.score) as score1, t2.id, t2.name, round(t2.score) as score2 from function_table t1, function_table_2 t2 where t1.id > t2.id and round(t1.score) > round(t2.score);

select date_format('2009-03-11', '%y/%m/%d') as date_type;
exit