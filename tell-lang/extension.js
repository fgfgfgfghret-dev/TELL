const vscode = require('vscode');

function activate(context) {
    // Регистрируем провайдер автодополнения для языка 'tell'
    const provider = vscode.languages.registerCompletionItemProvider('tell', {
        provideCompletionItems(document, position) {
            // Список всех команд
            const commands = [
                // Блоки и условия
                { label: 'WHILE', detail: 'Цикл', insertText: 'WHILE ' },
                { label: 'IF', detail: 'Условие', insertText: 'IF ' },
                { label: 'ELSE', detail: 'Иначе', insertText: 'ELSE:' },
                { label: 'END', detail: 'Конец блока', insertText: 'END' },
                { label: 'FOREACH', detail: 'Цикл по списку', insertText: 'FOREACH ' },
                { label: 'TRY', detail: 'Попытка', insertText: 'TRY\n    \nCATCH err\n    \nEND' },
                { label: 'THREAD', detail: 'Поток', insertText: 'THREAD ' },
                { label: 'SUB', detail: 'Под-язык', insertText: 'SUB(KROS)\n    \nEND' },
                
                // Переменные и математика
                { label: 'SET', detail: 'Присвоить', insertText: 'SET ' },
                { label: 'PRINT', detail: 'Вывод', insertText: 'PRINT ' },
                { label: 'ADD', detail: 'Добавить в список', insertText: 'ADD ' },
                { label: 'GET', detail: 'Взять из списка', insertText: 'GET ' },
                { label: 'LEN', detail: 'Длина', insertText: 'LEN ' },
                { label: 'POP', detail: 'Удалить последний', insertText: 'POP ' },
                { label: 'CLEAR', detail: 'Очистить', insertText: 'CLEAR ' },
                
                // Строки
                { label: 'CONCAT', detail: 'Склеить строки', insertText: 'CONCAT ' },
                { label: 'SUBSTR', detail: 'Подстрока', insertText: 'SUBSTR ' },
                { label: 'FIND', detail: 'Поиск в строке', insertText: 'FIND ' },
                { label: 'REPLACE', detail: 'Замена в строке', insertText: 'REPLACE ' },
                { label: 'TO_STR', detail: 'Число в строку', insertText: 'TO_STR ' },
                { label: 'TO_NUM', detail: 'Строку в число', insertText: 'TO_NUM ' },
                { label: 'UPPER', detail: 'В верхний регистр', insertText: 'UPPER ' },
                { label: 'LOWER', detail: 'В нижний регистр', insertText: 'LOWER ' },
                { label: 'TRIM', detail: 'Убрать пробелы', insertText: 'TRIM ' },
                { label: 'SPLIT', detail: 'Разбить строку', insertText: 'SPLIT ' },
                { label: 'JOIN', detail: 'Собрать строку', insertText: 'JOIN ' },
                
                // Управление и ввод/вывод
                { label: 'KEY', detail: 'Ждать нажатия клавиши', insertText: 'KEY' },
                { label: 'SLEEP', detail: 'Пауза (мс)', insertText: 'SLEEP ' },
                { label: 'IMPORT', detail: 'Импорт файла', insertText: 'IMPORT ' },
                { label: 'STOP', detail: 'Остановить', insertText: 'STOP' },
                { label: 'GOTO', detail: 'Переход', insertText: 'GOTO ' },
                { label: 'JHP', detail: 'Переход по условию', insertText: 'JHP ' },
                { label: 'LABEL', detail: 'Метка', insertText: 'LABEL ' },
                { label: 'HOL_JMP', detail: 'Таблица прыжков', insertText: 'HOL_JMP:\n    1_ (1): (' },
                
                // Встроенные команды (THN)
                { label: 'SQ', detail: 'Квадрат A', insertText: 'SQ' },
                { label: 'COUT', detail: 'Вывод A', insertText: 'COUT' },
                { label: 'BOOL', detail: 'Логическое A', insertText: 'BOOL' },
                { label: 'IU', detail: 'Ввод (верхний)', insertText: 'IU' },
                { label: 'LOU', detail: 'Ввод (нижний)', insertText: 'LOU' },
                { label: 'BOlN', detail: 'A в двоичном', insertText: 'BOlN' }
            ];

            // Превращаем список в объекты CompletionItem
            const suggestions = commands.map(cmd => {
                const item = new vscode.CompletionItem(cmd.label, vscode.CompletionItemKind.Keyword);
                item.detail = cmd.detail;
                item.insertText = cmd.insertText;
                return item;
            });

            return suggestions;
        }
    });

    context.subscriptions.push(provider);
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};